#include "request.h"

#include <assert.h>

bool natpmp_request_released(natpmp_request_t const *request) {
  return request->opcode != NATPMP_OPCODE_EXTERNAL_ADDRESS &&
         request->map.lifetime == 0;
}

bool natpmp_request_has_event(natpmp_request_t const *request) {
  return request->state == NATPMP_REQUEST_ANSWERED ||
         request->state == NATPMP_REQUEST_FAILED;
}

// the event for a request that has one
static natpmp_event_t natpmp_request_event(natpmp_request_t const *request) {
  assert(request);

  if (request->opcode == NATPMP_OPCODE_EXTERNAL_ADDRESS) {
    return request->state == NATPMP_REQUEST_FAILED
               ? (natpmp_event_t){.type = NATPMP_EVENT_ADDRESS_FAILED}
               : (natpmp_event_t){
                     .type = NATPMP_EVENT_ADDRESS,
                     .address = {.external_ip = request->external_ip},
                 };
  }

  if (request->state == NATPMP_REQUEST_FAILED) {
    return (natpmp_event_t){.type = NATPMP_EVENT_MAP_FAILED,
                            .map_failed = {.handle = request->handle}};
  }

  return (natpmp_event_t){
      .type = NATPMP_EVENT_MAPPED,
      .mapped = {.handle = request->handle,
                 .external_port = request->map.external_port,
                 .lifetime = request->map.lifetime},
  };
}

natpmp_event_t natpmp_request_take_event(natpmp_request_t *request) {
  assert(request);
  assert(natpmp_request_has_event(request));

  auto const event = natpmp_request_event(request);
  request->state = event.type == NATPMP_EVENT_MAPPED ? NATPMP_REQUEST_ACTIVE
                                                     : NATPMP_REQUEST_FREE;
  return event;
}

bool natpmp_request_matches(natpmp_request_t const *request,
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

void natpmp_request_apply_answer(natpmp_request_t *request,
                                 natpmp_response_t const *response,
                                 natpmp_time_ms_t now) {
  assert(request);
  assert(response);

  if (natpmp_request_released(request)) {
    // deleted, or refused and left to expire
    request->state = NATPMP_REQUEST_FREE;
    return;
  }

  switch (response->type) {
  case NATPMP_RESPONSE_ERROR:
    // refused, which only concerns this request
    request->state = NATPMP_REQUEST_FAILED;
    return;
  case NATPMP_RESPONSE_ADDRESS:
    request->state = NATPMP_REQUEST_ANSWERED;
    request->external_ip = response->address.external_ip;
    return;
  case NATPMP_RESPONSE_MAP:
    if (response->map.lifetime == 0) {
      // there's no mapping, and renewing it would delete it
      request->state = NATPMP_REQUEST_FAILED;
      return;
    }
    request->state = NATPMP_REQUEST_ANSWERED;
    request->map.external_port = response->map.external_port;
    request->map.lifetime = response->map.lifetime;
    // halfway through the lifetime
    request->map.renew_at =
        now + (natpmp_time_ms_t)response->map.lifetime * 500;
    return;
  }
}

void natpmp_request_fail(natpmp_request_t *request) {
  assert(request);
  request->state = natpmp_request_released(request) ? NATPMP_REQUEST_FREE
                                                    : NATPMP_REQUEST_FAILED;
}

bool natpmp_request_release(natpmp_request_t *request) {
  assert(request);
  assert(request->opcode != NATPMP_OPCODE_EXTERNAL_ADDRESS);

  if (natpmp_request_released(request)) {
    return false;
  }

  // what a deletion asks for
  request->map.external_port = 0;
  request->map.lifetime = 0;

  switch (request->state) {
  case NATPMP_REQUEST_FREE:
    // not in use
    unreachable();
  case NATPMP_REQUEST_QUEUED:
    // goes out as a deletion when its turn comes
    return false;
  case NATPMP_REQUEST_SENT:
    // still the head, and goes out again right away as a deletion
    request->state = NATPMP_REQUEST_QUEUED;
    return false;
  case NATPMP_REQUEST_ANSWERED:
  case NATPMP_REQUEST_ACTIVE:
    return true;
  case NATPMP_REQUEST_FAILED:
    // there's nothing to delete, and its failure goes unreported
    request->state = NATPMP_REQUEST_FREE;
    return false;
  }
  unreachable();
}
