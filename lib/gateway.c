#include "gateway.h"

#include <assert.h>

bool natpmp_gateway_update_epoch(natpmp_gateway_t *gateway,
                                 natpmp_time_ms_t now,
                                 uint32_t seconds) {
  assert(gateway);

  auto const last = gateway->epoch;
  gateway->epoch = (typeof(gateway->epoch)){.has_value = true,
                                            .seconds = seconds,
                                            .received = now};
  if (!last.has_value) {
    return false;
  }

  assert(now >= last.received);
  // in eighths of a millisecond
  return (uint64_t)seconds * 8000 + 16000 <
         (uint64_t)last.seconds * 8000 + 7 * (now - last.received);
}
