#pragma once

#include <byo/natpmp/natpmp.h>

/**
 * @brief What the gateway told the session about itself
 *
 * Starts zeroed, before any response.
 */
typedef struct natpmp_gateway {
  /// The Seconds Since Start of Epoch from the last response
  struct {
    bool has_value;
    uint32_t seconds;
    /// When that response arrived
    natpmp_time_ms_t received;
  } epoch;
} natpmp_gateway_t;

/**
 * @brief Record the epoch of a response that arrived at `now`
 *
 * @returns Whether the gateway lost its mappings since the last response,
 *          going by an epoch more than 2 s behind 7/8 of the time since
 */
bool natpmp_gateway_update_epoch(natpmp_gateway_t *gateway,
                                 natpmp_time_ms_t now,
                                 uint32_t seconds);
