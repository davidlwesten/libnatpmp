#include "request.h"

#include <assert.h>

bool natpmp_request_released(natpmp_request_t const *request) {
  return request->opcode != NATPMP_OPCODE_EXTERNAL_ADDRESS &&
         request->map.lifetime == 0;
}

bool natpmp_request_done(natpmp_request_t const *request) {
  return request->state == NATPMP_REQUEST_ANSWERED ||
         request->state == NATPMP_REQUEST_FAILED;
}

natpmp_event_t natpmp_request_event(natpmp_request_t const *request) {
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
