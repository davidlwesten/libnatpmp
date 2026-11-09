#include <byo/natpmp/natpmp.h>

#include "packet.h"
#include "retransmit.h"

#include <assert.h>
#include <errno.h>
#include <stdlib.h>

#define ALLOC(allocator, ...) (allocator.alloc(allocator.ctx, __VA_ARGS__))
#define FREE(allocator, ...) (allocator.free(allocator.ctx, __VA_ARGS__))

static constexpr uint16_t NATPMP_DEFAULT_QUEUE_SIZE = 8;

typedef struct natpmp_request {
  natpmp_handle_t handle;
  enum {
    /// Waiting to go out, or for the gateway's answer
    PENDING,
    ANSWERED,
    FAILED,
  } state;
  /// Once ANSWERED
  natpmp_ipv4_address_t external_ip;
} natpmp_request_t;

struct natpmp_session {
  natpmp_config_t config;
  natpmp_handle_t handles;
  /// For the request at the head of the queue
  natpmp_retransmit_t retransmit;
  uint8_t outgoing_data[NATPMP_ADDRESS_REQUEST_MAX_SIZE];
  size_t queue_head;
  size_t queue_count;
  /// A ring buffer of `config.queue_size` requests, each kept until its last
  /// event is polled. The done ones always come first, and only the first
  /// pending one is ever on the wire.
  natpmp_request_t queue[];
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

  if (config.queue_size == 0) {
    config.queue_size = NATPMP_DEFAULT_QUEUE_SIZE;
  }

  natpmp_session_t *session =
      ALLOC(config.allocator, sizeof(natpmp_session_t) +
                                  config.queue_size * sizeof(natpmp_request_t));
  if (session) {
    *session = (natpmp_session_t){.config = config};
  }

  return session;
}

void natpmp_session_end(natpmp_session_t *session) {
  assert(session);
  FREE(session->config.allocator, session);
}

natpmp_handle_or_error_t
natpmp_session_address_request(natpmp_session_t *session) {
  assert(session);

  if (session->queue_count == session->config.queue_size) {
    return (natpmp_handle_or_error_t){.has_value = false, .error = ENOBUFS};
  }

  auto const request =
      &session->queue[(session->queue_head + session->queue_count) %
                      session->config.queue_size];
  *request = (natpmp_request_t){.handle = ++session->handles, .state = PENDING};
  session->queue_count += 1;

  return (natpmp_handle_or_error_t){.has_value = true,
                                    .value = request->handle};
}

static natpmp_request_t *natpmp_session_at(natpmp_session_t *session,
                                           size_t index) {
  return &session->queue[(session->queue_head + index) %
                         session->config.queue_size];
}

// the request on the wire is done, the next one starts from scratch
static void natpmp_session_request_done(natpmp_session_t *session) {
  session->retransmit = (natpmp_retransmit_t){};
}

// the gateway is gone, so everything still waiting fails with the head
static void natpmp_session_fail_all(natpmp_session_t *session) {
  for (size_t i = 0; i < session->queue_count; ++i) {
    auto const request = natpmp_session_at(session, i);
    if (request->state == PENDING) {
      request->state = FAILED;
    }
  }
  natpmp_session_request_done(session);
}

natpmp_event_t natpmp_session_poll(natpmp_session_t *session,
                                   natpmp_time_ms_t now) {
  assert(session);

  if (session->queue_count == 0) {
    return (natpmp_event_t){.type = NATPMP_EVENT_TIMEOUT};
  }

  // report the requests that are done first
  auto const head = natpmp_session_at(session, 0);
  if (head->state != PENDING) {
    auto const event =
        head->state == ANSWERED
            ? (natpmp_event_t){.type = NATPMP_EVENT_ADDRESS,
                               .address = {.handle = head->handle,
                                           .external_ip = head->external_ip}}
            : (natpmp_event_t){.type = NATPMP_EVENT_FAILED,
                               .failed = {.handle = head->handle}};
    session->queue_head =
        (session->queue_head + 1) % session->config.queue_size;
    session->queue_count -= 1;
    return event;
  }

  switch (natpmp_retransmit_poll(&session->retransmit, now)) {
  case NATPMP_RETRANSMIT_WAIT:
    break;
  case NATPMP_RETRANSMIT_DUE: {
    auto const size = natpmp_packet_write_address_request(
        (view_t){.size = sizeof(session->outgoing_data),
                 .data = session->outgoing_data});
    natpmp_retransmit_advance(&session->retransmit, now);
    return (natpmp_event_t){
        .type = NATPMP_EVENT_SEND,
        .send = {.size = size, .data = session->outgoing_data},
    };
  }
  case NATPMP_RETRANSMIT_GAVE_UP:
    natpmp_session_fail_all(session);
    // there are FAILED events to report now
    return natpmp_session_poll(session, now);
  }

  return (natpmp_event_t){
      .type = NATPMP_EVENT_TIMEOUT,
      .timeout =
          {
              .deadline = {.has_value = true,
                           .value = session->retransmit.next},
          },
  };
}

void natpmp_session_on_datagram(natpmp_session_t *session,
                                size_t size,
                                uint8_t const data[]) {
  assert(session);
  assert(data || size == 0);

  // nothing to wait for without a request on the wire, which is always the
  // head then
  if (session->retransmit.count == 0) {
    return;
  }
  auto const head = natpmp_session_at(session, 0);
  assert(session->queue_count > 0 && head->state == PENDING);

  auto const response =
      natpmp_packet_read_response((const_view_t){.size = size, .data = data});
  if (!response.has_value) {
    return;
  }

  auto const r = &response.value;
  if (r->type == NATPMP_RESPONSE_ADDRESS) {
    head->state = ANSWERED;
    head->external_ip = r->address.external_ip;
    natpmp_session_request_done(session);
  } else if (r->type == NATPMP_RESPONSE_ERROR &&
             r->error.opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS) {
    // refused, which only concerns this request
    head->state = FAILED;
    natpmp_session_request_done(session);
  }
}

void natpmp_session_on_unreachable(natpmp_session_t *session) {
  assert(session);
  natpmp_session_fail_all(session);
}
