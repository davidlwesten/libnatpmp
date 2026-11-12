#include "epoch.h"

#include <assert.h>

bool natpmp_epoch_update(natpmp_epoch_t *epoch,
                         natpmp_time_ms_t now,
                         uint32_t seconds) {
  assert(epoch);

  auto const last = *epoch;
  *epoch =
      (natpmp_epoch_t){.has_value = true, .seconds = seconds, .received = now};
  if (!last.has_value) {
    return false;
  }

  assert(now >= last.received);
  // in eighths of a millisecond
  return (uint64_t)seconds * 8000 + 16000 <
         (uint64_t)last.seconds * 8000 + 7 * (now - last.received);
}
