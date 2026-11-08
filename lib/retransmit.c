#include "retransmit.h"

#include <assert.h>

// the first wait, doubling after every transmission
static constexpr natpmp_time_ms_t NATPMP_RETRANSMIT_FIRST_WAIT = 250;

static constexpr uint32_t NATPMP_RETRANSMIT_ATTEMPTS = 9;

natpmp_retransmit_status_t
natpmp_retransmit_poll(natpmp_retransmit_t const *retransmit,
                       natpmp_time_ms_t now) {
  assert(retransmit);

  if (now < retransmit->next) {
    return NATPMP_RETRANSMIT_WAIT;
  }
  return retransmit->count < NATPMP_RETRANSMIT_ATTEMPTS
             ? NATPMP_RETRANSMIT_DUE
             : NATPMP_RETRANSMIT_GAVE_UP;
}

void natpmp_retransmit_advance(natpmp_retransmit_t *retransmit,
                               natpmp_time_ms_t now) {
  assert(retransmit);
  assert(retransmit->count < NATPMP_RETRANSMIT_ATTEMPTS);

  retransmit->next = now + (NATPMP_RETRANSMIT_FIRST_WAIT << retransmit->count);
  retransmit->count += 1;
}
