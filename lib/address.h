#pragma once

#include <stdint.h>

/**
 * @brief An IPv4 address, the only kind NAT-PMP knows
 */
typedef struct natpmp_ipv4_address {
  uint8_t octets[4];
} natpmp_ipv4_address_t;
