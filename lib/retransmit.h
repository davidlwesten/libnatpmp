#pragma once

#include <byo/natpmp/natpmp.h>

/**
 * @brief When to send a request again
 *
 * Starts zeroed, with the first transmission due right away. After that the
 * client waits 250 ms, doubling every time, and gives up 64 s after the ninth.
 */
typedef struct natpmp_retransmit {
  /// Transmissions so far
  uint32_t count;
  /// The next transmission, or when to give up after the last one
  natpmp_time_ms_t next;
} natpmp_retransmit_t;

typedef enum natpmp_retransmit_status {
  NATPMP_RETRANSMIT_WAIT,
  NATPMP_RETRANSMIT_DUE,
  /// No response to the last transmission either
  NATPMP_RETRANSMIT_GAVE_UP,
} natpmp_retransmit_status_t;

natpmp_retransmit_status_t
natpmp_retransmit_poll(natpmp_retransmit_t const *retransmit,
                       natpmp_time_ms_t now);

/**
 * @brief Record a transmission at `now`
 */
void natpmp_retransmit_advance(natpmp_retransmit_t *retransmit,
                               natpmp_time_ms_t now);
