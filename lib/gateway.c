#include "gateway.h"

#include <assert.h>
#include <string.h>

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

bool natpmp_gateway_update_external_ip(natpmp_gateway_t *gateway,
                                       natpmp_ipv4_address_t external_ip) {
  assert(gateway);

  auto const last = gateway->external_ip;
  gateway->external_ip =
      (typeof(gateway->external_ip)){.has_value = true, .value = external_ip};
  return !last.has_value || memcmp(last.value.octets, external_ip.octets,
                                   sizeof(external_ip.octets)) != 0;
}
