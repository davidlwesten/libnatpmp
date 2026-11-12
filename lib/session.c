#include <byo/natpmp/natpmp.h>

#include "packet.h"
#include "retransmit.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#define ALLOC(allocator, ...) (allocator.alloc(allocator.ctx, __VA_ARGS__))
#define FREE(allocator, ...) (allocator.free(allocator.ctx, __VA_ARGS__))

/**
 * @brief A request, kept in its slot from when it's made until its last event
 *
 * A slot moves through these states:
 *
 *   FREE        -> QUEUED    a request is made in it
 *   QUEUED      -> SENT      it's at the head of the queue and goes out
 *   SENT        -> ANSWERED  the gateway answers it
 *   SENT        -> FAILED    the gateway refuses it, or maps it for no time
 *   QUEUED/SENT -> FAILED    the head is given up on or the gateway can't be
 *                            reached, which fails everything queued
 *   ANSWERED    -> FREE      an external address request's answer is polled
 *   ANSWERED    -> ACTIVE    a mapping's answer is polled
 *   FAILED      -> FREE      its failure is polled
 *   ACTIVE      -> QUEUED    `map.renew_at` passes, and it joins the back of
 *                            the queue
 *
 * Releasing a mapping makes it ask for its deletion, and frees it without an
 * event once that's answered, refused or given up on. A QUEUED one keeps its
 * place in the queue:
 *
 *   ANSWERED/ACTIVE -> QUEUED  it joins the back of the queue
 *   SENT            -> QUEUED  it goes out again right away
 *   QUEUED/SENT     -> FREE    its deletion is done
 *   FAILED          -> FREE    right away, there's nothing to delete
 *
 * A request is in the queue exactly while it's QUEUED or SENT, and only the
 * head is ever SENT. It stays SENT while it's retransmitted.
 */
typedef struct natpmp_request {
  /// The request behind this one in the queue
  struct natpmp_request *next;
  natpmp_handle_t handle;
  /// Which request this is, and so which member of the union is in use
  enum natpmp_opcode opcode;
  enum {
    FREE,
    /// Waiting to go out
    QUEUED,
    /// On the wire, waiting for the gateway's answer
    SENT,
    ANSWERED,
    FAILED,
    /// A mapping that was reported, until `map.renew_at`
    ACTIVE,
  } state;
  union {
    /// Once ANSWERED
    natpmp_ipv4_address_t external_ip;
    /// `external_port` and `lifetime` are asked for, then granted once
    /// ANSWERED, and renewals ask for them again. Both are 0 once released.
    struct {
      natpmp_port_t internal_port;
      natpmp_port_t external_port;
      natpmp_time_s_t lifetime;
      natpmp_time_ms_t renew_at;
    } map;
  };
} natpmp_request_t;

// a mapping asks for its deletion with lifetime 0, which it's never granted
static bool natpmp_request_released(natpmp_request_t const *request) {
  return request->opcode != NATPMP_OPCODE_EXTERNAL_ADDRESS &&
         request->map.lifetime == 0;
}

// the event for a request that's done
static natpmp_event_t natpmp_request_event(natpmp_request_t const *request) {
  assert(request);

  if (request->state == FAILED) {
    return (natpmp_event_t){.type = NATPMP_EVENT_FAILED,
                            .failed = {.handle = request->handle}};
  }

  if (request->opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS) {
    return (natpmp_event_t){
        .type = NATPMP_EVENT_ADDRESS,
        .address = {.handle = request->handle,
                    .external_ip = request->external_ip},
    };
  }

  return (natpmp_event_t){
      .type = NATPMP_EVENT_MAPPED,
      .mapped = {.handle = request->handle,
                 .external_port = request->map.external_port,
                 .lifetime = request->map.lifetime},
  };
}

static bool natpmp_request_matches(natpmp_request_t const *request,
                                   natpmp_response_t const *response) {
  assert(request);
  assert(response);

  switch (response->type) {
  case NATPMP_RESPONSE_ERROR:
    return response->error.opcode == request->opcode;
  case NATPMP_RESPONSE_ADDRESS:
    return request->opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS;
  case NATPMP_RESPONSE_MAP:
    // a released mapping waits for its deletion, not a late grant
    return response->map.opcode == request->opcode &&
           response->map.internal_port == request->map.internal_port &&
           (!natpmp_request_released(request) || response->map.lifetime == 0);
  }
  unreachable();
}

static void natpmp_request_apply_answer(natpmp_request_t *request,
                                        natpmp_response_t const *response,
                                        natpmp_time_ms_t now) {
  assert(request);
  assert(response);

  if (natpmp_request_released(request)) {
    // deleted, or refused and left to expire
    request->state = FREE;
    return;
  }

  switch (response->type) {
  case NATPMP_RESPONSE_ERROR:
    // refused, which only concerns this request
    request->state = FAILED;
    return;
  case NATPMP_RESPONSE_ADDRESS:
    request->state = ANSWERED;
    request->external_ip = response->address.external_ip;
    return;
  case NATPMP_RESPONSE_MAP:
    if (response->map.lifetime == 0) {
      // there's no mapping, and renewing it would delete it
      request->state = FAILED;
      return;
    }
    request->state = ANSWERED;
    request->map.external_port = response->map.external_port;
    request->map.lifetime = response->map.lifetime;
    // halfway through the lifetime
    request->map.renew_at =
        now + (natpmp_time_ms_t)response->map.lifetime * 500;
    return;
  }
}

typedef struct natpmp_outgoing {
  /// The request going out, with the QUEUED ones behind it through `next`, in
  /// the order they were made or came up for renewal. Only it is ever SENT.
  natpmp_request_t *request;
  /// For `request`
  natpmp_retransmit_t retransmit;
  uint8_t data[NATPMP_MAP_REQUEST_MAX_SIZE];
} natpmp_outgoing_t;

typedef struct natpmp_outgoing_status {
  natpmp_retransmit_status_t status;
  /// How much of `data` to send, when DUE
  size_t size;
} natpmp_outgoing_status_t;

// puts the request at the back of the queue
static void natpmp_outgoing_push(natpmp_outgoing_t *outgoing,
                                 natpmp_request_t *request) {
  natpmp_request_t **last = &outgoing->request;
  while (*last) {
    last = &(*last)->next;
  }
  request->next = nullptr;
  *last = request;
}

static void natpmp_outgoing_pop(natpmp_outgoing_t *outgoing) {
  outgoing->request = outgoing->request->next;
}

// the gateway is gone, so everything still waiting fails with the head
static void natpmp_outgoing_fail_all(natpmp_outgoing_t *outgoing) {
  while (outgoing->request) {
    // a released mapping just expires on the gateway
    outgoing->request->state =
        natpmp_request_released(outgoing->request) ? FREE : FAILED;
    natpmp_outgoing_pop(outgoing);
  }
}

// writes `request` into `data` when it's due
static natpmp_outgoing_status_t
natpmp_outgoing_poll(natpmp_outgoing_t *outgoing, natpmp_time_ms_t now) {
  assert(outgoing);

  auto const request = outgoing->request;
  assert(request);

  if (request->state == QUEUED) {
    // a new head, whose retransmissions start from scratch
    outgoing->retransmit = (natpmp_retransmit_t){};
  }

  auto const status = natpmp_retransmit_poll(&outgoing->retransmit, now);
  if (status != NATPMP_RETRANSMIT_DUE) {
    return (natpmp_outgoing_status_t){.status = status};
  }

  view_t const dst = {.size = sizeof(outgoing->data), .data = outgoing->data};
  auto const size =
      request->opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS
          ? natpmp_packet_write_address_request(dst)
          : natpmp_packet_write_map_request(
                dst, request->opcode, request->map.internal_port,
                request->map.external_port, request->map.lifetime);
  request->state = SENT;
  natpmp_retransmit_advance(&outgoing->retransmit, now);
  return (natpmp_outgoing_status_t){.status = NATPMP_RETRANSMIT_DUE,
                                    .size = size};
}

// answers the request on the wire, if the response is for it
static void natpmp_outgoing_on_response(natpmp_outgoing_t *outgoing,
                                        natpmp_time_ms_t now,
                                        natpmp_response_t const *response) {
  assert(outgoing);
  assert(response);

  if (!outgoing->request || outgoing->request->state != SENT ||
      !natpmp_request_matches(outgoing->request, response)) {
    return;
  }

  natpmp_request_apply_answer(outgoing->request, response, now);
  natpmp_outgoing_pop(outgoing);
}

static constexpr uint16_t NATPMP_DEFAULT_CAPACITY = 8;

struct natpmp_session {
  natpmp_config_t config;
  natpmp_handle_t handles;
  natpmp_outgoing_t outgoing;
  /// `config.capacity` slots, each request kept until its last event is
  /// polled, and each mapping until it fails or its deletion is done
  natpmp_request_t requests[];
};

static void *natpmp_default_alloc(void * /*ctx*/, size_t size) {
  return malloc(size);
}

static void natpmp_default_free(void * /*ctx*/, void *ptr) { free(ptr); }

natpmp_session_t *natpmp_session_begin(natpmp_config_t config) {
  if (!config.allocator.alloc && !config.allocator.free) {
    config.allocator.alloc = natpmp_default_alloc;
    config.allocator.free = natpmp_default_free;
  }
  assert(config.allocator.alloc && config.allocator.free);

  if (config.capacity == 0) {
    config.capacity = NATPMP_DEFAULT_CAPACITY;
  }

  natpmp_session_t *session =
      ALLOC(config.allocator, sizeof(natpmp_session_t) +
                                  config.capacity * sizeof(natpmp_request_t));
  if (session) {
    *session = (natpmp_session_t){.config = config};
    auto const begin = session->requests;
    auto const end = begin + config.capacity;
    for (auto request = begin; request != end; ++request) {
      *request = (natpmp_request_t){.state = FREE};
    }
  }

  return session;
}

void natpmp_session_end(natpmp_session_t *session) {
  assert(session);
  FREE(session->config.allocator, session);
}

static natpmp_handle_or_error_t natpmp_session_add(natpmp_session_t *session,
                                                   natpmp_request_t request) {
  auto const begin = session->requests;
  auto const end = begin + session->config.capacity;
  for (auto slot = begin; slot != end; ++slot) {
    if (slot->state == FREE) {
      request.handle = ++session->handles;
      request.state = QUEUED;
      *slot = request;
      natpmp_outgoing_push(&session->outgoing, slot);
      return (natpmp_handle_or_error_t){.has_value = true,
                                        .value = request.handle};
    }
  }

  return (natpmp_handle_or_error_t){.has_value = false, .error = ENOBUFS};
}

natpmp_handle_or_error_t
natpmp_session_address_request(natpmp_session_t *session) {
  assert(session);
  return natpmp_session_add(
      session, (natpmp_request_t){.opcode = NATPMP_OPCODE_EXTERNAL_ADDRESS});
}

natpmp_handle_or_error_t
natpmp_session_map_request(natpmp_session_t *session,
                           natpmp_time_s_t lifetime,
                           natpmp_protocol_t protocol,
                           natpmp_port_t port,
                           natpmp_port_t external_port) {
  assert(session);
  // lifetime 0 means delete, and port 0 deletes all mappings
  assert(lifetime > 0);
  assert(port > 0);
  assert(protocol == NATPMP_PROTOCOL_UDP || protocol == NATPMP_PROTOCOL_TCP);

  return natpmp_session_add(session,
                            (natpmp_request_t){
                                .opcode = protocol == NATPMP_PROTOCOL_UDP
                                              ? NATPMP_OPCODE_MAP_UDP
                                              : NATPMP_OPCODE_MAP_TCP,
                                .map = {.internal_port = port,
                                        .external_port = external_port,
                                        .lifetime = lifetime},
                            });
}

// the request with the handle, if it's still in its slot
static natpmp_request_t *natpmp_session_find(natpmp_session_t *session,
                                             natpmp_handle_t handle) {
  assert(session);

  auto const begin = session->requests;
  auto const end = begin + session->config.capacity;
  for (auto request = begin; request != end; ++request) {
    if (request->state != FREE && request->handle == handle) {
      return request;
    }
  }

  return nullptr;
}

void natpmp_session_release(natpmp_session_t *session, natpmp_handle_t handle) {
  assert(session);

  auto const request = natpmp_session_find(session, handle);
  if (!request || request->opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS ||
      natpmp_request_released(request)) {
    return;
  }

  // what a deletion asks for
  request->map.external_port = 0;
  request->map.lifetime = 0;

  switch (request->state) {
  case FREE:
    // natpmp_session_find skips free slots
    unreachable();
  case QUEUED:
    // goes out as a deletion when its turn comes
    return;
  case SENT:
    // still the head, and goes out again right away as a deletion
    request->state = QUEUED;
    return;
  case ANSWERED:
  case ACTIVE:
    request->state = QUEUED;
    natpmp_outgoing_push(&session->outgoing, request);
    return;
  case FAILED:
    // there's nothing to delete, and its failure goes unreported
    request->state = FREE;
    return;
  }
}

// a request whose last event is still to be polled, if any
static natpmp_request_t *natpmp_session_done(natpmp_session_t *session) {
  assert(session);

  auto const begin = session->requests;
  auto const end = begin + session->config.capacity;
  for (auto request = begin; request != end; ++request) {
    if (request->state == ANSWERED || request->state == FAILED) {
      return request;
    }
  }

  return nullptr;
}

// queues the mappings that are due for renewal, and returns when the next one
// is due
static natpmp_optional_time_ms_t natpmp_session_renew(natpmp_session_t *session,
                                                      natpmp_time_ms_t now) {
  assert(session);

  natpmp_optional_time_ms_t next = {};

  auto const begin = session->requests;
  auto const end = begin + session->config.capacity;
  for (auto request = begin; request != end; ++request) {
    if (request->state != ACTIVE) {
      continue;
    }

    if (request->map.renew_at <= now) {
      request->state = QUEUED;
      natpmp_outgoing_push(&session->outgoing, request);
    } else if (!next.has_value || request->map.renew_at < next.value) {
      next = (natpmp_optional_time_ms_t){.has_value = true,
                                         .value = request->map.renew_at};
    }
  }

  return next;
}

natpmp_event_t natpmp_session_poll(natpmp_session_t *session,
                                   natpmp_time_ms_t now) {
  assert(session);

  // report what's answered or failed before sending anything
  auto const done = natpmp_session_done(session);
  if (done) {
    auto const event = natpmp_request_event(done);
    done->state = event.type == NATPMP_EVENT_MAPPED ? ACTIVE : FREE;
    return event;
  }

  // queue any renewals that are due, and if nothing is ready to send, return it
  // as the session's timeout
  auto const renewal = natpmp_session_renew(session, now);
  if (!session->outgoing.request) {
    return (natpmp_event_t){.type = NATPMP_EVENT_TIMEOUT,
                            .timeout = {.deadline = renewal}};
  }

  // send the head of the queue, or wait for its answer
  auto const out = natpmp_outgoing_poll(&session->outgoing, now);
  switch (out.status) {
  case NATPMP_RETRANSMIT_WAIT:
    return (natpmp_event_t){
        .type = NATPMP_EVENT_TIMEOUT,
        .timeout = {.deadline = {.has_value = true,
                                 .value = session->outgoing.retransmit.next}},
    };
  case NATPMP_RETRANSMIT_DUE:
    return (natpmp_event_t){
        .type = NATPMP_EVENT_SEND,
        .send = {.size = out.size, .data = session->outgoing.data},
    };
  case NATPMP_RETRANSMIT_GAVE_UP:
    natpmp_outgoing_fail_all(&session->outgoing);
    // there are FAILED events to report now
    return natpmp_session_poll(session, now);
  }
  unreachable();
}

void natpmp_session_on_datagram(natpmp_session_t *session,
                                natpmp_time_ms_t now,
                                size_t size,
                                uint8_t const data[]) {
  assert(session);
  assert(data || size == 0);

  auto const response =
      natpmp_packet_read_response((const_view_t){.size = size, .data = data});
  if (!response.has_value) {
    return;
  }

  natpmp_outgoing_on_response(&session->outgoing, now, &response.value);
}

void natpmp_session_on_host_unreachable(natpmp_session_t *session) {
  assert(session);
  natpmp_outgoing_fail_all(&session->outgoing);
}
