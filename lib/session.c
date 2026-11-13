#include <byo/natpmp/natpmp.h>

#include "gateway.h"
#include "packet.h"
#include "queue.h"
#include "request.h"
#include "retransmit.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#define ALLOC(allocator, ...) (allocator.alloc(allocator.ctx, __VA_ARGS__))
#define FREE(allocator, ...) (allocator.free(allocator.ctx, __VA_ARGS__))

static constexpr uint16_t NATPMP_DEFAULT_CAPACITY = 8;

struct natpmp_session {
  natpmp_config_t config;
  natpmp_handle_t handles;
  natpmp_gateway_t gateway;
  natpmp_queue_t outgoing;
  /// Shared by every caller until its event is polled
  natpmp_request_t address_request;
  /// `config.capacity` slots for mappings, each kept until its failure is
  /// polled or its deletion is done
  natpmp_request_t map_requests[];
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
  assert(config.rng.fill);

  if (config.capacity == 0) {
    config.capacity = NATPMP_DEFAULT_CAPACITY;
  }

  natpmp_session_t *session =
      ALLOC(config.allocator, sizeof(natpmp_session_t) +
                                  config.capacity * sizeof(natpmp_request_t));
  if (session) {
    *session = (natpmp_session_t){
        .config = config,
        .address_request = {.opcode = NATPMP_OPCODE_EXTERNAL_ADDRESS,
                            .state = NATPMP_REQUEST_FREE},
    };
    auto const begin = session->map_requests;
    auto const end = begin + config.capacity;
    for (auto request = begin; request != end; ++request) {
      *request = (natpmp_request_t){.state = NATPMP_REQUEST_FREE};
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
  auto const begin = session->map_requests;
  auto const end = begin + session->config.capacity;
  for (auto slot = begin; slot != end; ++slot) {
    if (slot->state == NATPMP_REQUEST_FREE) {
      request.handle = ++session->handles;
      *slot = request;
      natpmp_queue_push(&session->outgoing, slot);
      return (natpmp_handle_or_error_t){.has_value = true,
                                        .value = request.handle};
    }
  }

  return (natpmp_handle_or_error_t){.has_value = false, .error = ENOBUFS};
}

void natpmp_session_address_request(natpmp_session_t *session) {
  assert(session);

  if (session->address_request.state != NATPMP_REQUEST_FREE) {
    // the earlier request's event answers this one too
    return;
  }

  natpmp_queue_push(&session->outgoing, &session->address_request);
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

  auto const begin = session->map_requests;
  auto const end = begin + session->config.capacity;
  for (auto request = begin; request != end; ++request) {
    if (request->state != NATPMP_REQUEST_FREE && request->handle == handle) {
      return request;
    }
  }

  return nullptr;
}

void natpmp_session_release(natpmp_session_t *session, natpmp_handle_t handle) {
  assert(session);

  auto const request = natpmp_session_find(session, handle);
  if (request && natpmp_request_release(request)) {
    natpmp_queue_push(&session->outgoing, request);
  }
}

// a request whose last event is still to be polled, if any
static natpmp_request_t *natpmp_session_done(natpmp_session_t *session) {
  assert(session);

  if (natpmp_request_done(&session->address_request)) {
    return &session->address_request;
  }

  auto const begin = session->map_requests;
  auto const end = begin + session->config.capacity;
  for (auto request = begin; request != end; ++request) {
    if (natpmp_request_done(request)) {
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

  auto const begin = session->map_requests;
  auto const end = begin + session->config.capacity;
  for (auto request = begin; request != end; ++request) {
    if (request->state != NATPMP_REQUEST_ACTIVE) {
      continue;
    }

    if (request->map.renew_at <= now) {
      natpmp_queue_push(&session->outgoing, request);
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
    return natpmp_request_take_event(done);
  }

  // queue any renewals that are due, and if nothing is ready to send, return it
  // as the session's timeout
  auto const renewal = natpmp_session_renew(session, now);
  if (!session->outgoing.head) {
    return (natpmp_event_t){.type = NATPMP_EVENT_TIMEOUT,
                            .timeout = {.deadline = renewal}};
  }

  // send the head of the queue, or wait for its answer
  auto const out = natpmp_queue_poll(&session->outgoing, now);
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
    natpmp_queue_fail_all(&session->outgoing);
    // there are FAILED events to report now
    return natpmp_session_poll(session, now);
  }
  unreachable();
}

// renews every mapping within 5 s, after the gateway lost them
static void natpmp_session_recover(natpmp_session_t *session,
                                   natpmp_time_ms_t now) {
  assert(session);

  // a random delay spreads out the clients that all saw the gateway reboot
  uint32_t random = 0;
  if (!session->config.rng.fill(session->config.rng.ctx, sizeof(random),
                                (uint8_t *)&random)) {
    random = 0;
  }
  auto const renew_at = now + random % 5001;

  auto const begin = session->map_requests;
  auto const end = begin + session->config.capacity;
  for (auto request = begin; request != end; ++request) {
    if (request->state == NATPMP_REQUEST_ANSWERED ||
        request->state == NATPMP_REQUEST_ACTIVE) {
      request->map.renew_at = renew_at;
    }
  }
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

  // the response still answers the request on the wire, which went to the
  // gateway as it is now, so only the mappings granted before it get renewed
  if (natpmp_gateway_update_epoch(&session->gateway, now,
                                  response.value.epoch)) {
    natpmp_session_recover(session, now);
  }

  // a repeat isn't news
  if (response.value.type == NATPMP_RESPONSE_ADDRESS &&
      natpmp_gateway_update_external_ip(&session->gateway,
                                        response.value.address.external_ip)) {
    // if the address request is waiting for an answer, let the gateway's
    // response answer it. Otherwise report the new address to the application.
    auto const request = &session->address_request;
    if (request->state != NATPMP_REQUEST_QUEUED &&
        request->state != NATPMP_REQUEST_SENT) {
      request->state = NATPMP_REQUEST_ANSWERED;
      request->external_ip = response.value.address.external_ip;
    }
  }

  natpmp_queue_on_response(&session->outgoing, now, &response.value);
}

void natpmp_session_on_host_unreachable(natpmp_session_t *session) {
  assert(session);
  natpmp_queue_fail_all(&session->outgoing);
}
