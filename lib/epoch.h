#pragma once

#include <byo/natpmp/natpmp.h>

/**
 * @brief The gateway's Seconds Since Start of Epoch as of the last response
 *
 * Starts zeroed, before any response.
 */
typedef struct natpmp_epoch {
  bool has_value;
  uint32_t seconds;
  /// When that response arrived
  natpmp_time_ms_t received;
} natpmp_epoch_t;

/**
 * @brief Record the epoch of a response that arrived at `now`
 *
 * @returns Whether the gateway lost its mappings since the last response,
 *          going by an epoch more than 2 s behind 7/8 of the time since
 */
bool natpmp_epoch_update(natpmp_epoch_t *epoch,
                         natpmp_time_ms_t now,
                         uint32_t seconds);
