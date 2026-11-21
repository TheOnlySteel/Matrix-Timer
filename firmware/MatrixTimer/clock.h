// Clock: time is computed, never counted (design.md §6).
//   remaining = duration - (now - started_at - paused_total)
// Pure: no Arduino dependencies, so it runs in the host test too.
#pragma once
#include <stdint.h>

struct Clock {
  int64_t duration_us = 0;
  int64_t started_us = 0;
  int64_t paused_total_us = 0;
  int64_t pause_start_us = 0;
  bool active = false;   // a block exists (running or paused)
  bool paused = false;

  void start(int64_t now, int64_t duration) {
    duration_us = duration;
    started_us = now;
    paused_total_us = 0;
    active = true;
    paused = false;
  }

  // Resume a block saved as "remaining" (monotonic time doesn't survive a reset).
  void restore(int64_t now, int64_t duration, int64_t remaining, bool asPaused) {
    if (remaining > duration) remaining = duration;
    start(now - (duration - remaining), duration);
    if (asPaused) pause(now);
  }

  void pause(int64_t now) {
    if (active && !paused) { paused = true; pause_start_us = now; }
  }
  void resume(int64_t now) {
    if (active && paused) { paused_total_us += now - pause_start_us; paused = false; }
  }
  void cancel() { active = paused = false; }

  // +5 min: the block gets longer, so the grid re-spreads over the new total.
  void extend(int64_t add) { if (active) duration_us += add; }

  int64_t elapsed(int64_t now) const {
    int64_t end = paused ? pause_start_us : now;
    return end - started_us - paused_total_us;
  }
  int64_t remaining(int64_t now) const {
    if (!active) return 0;
    int64_t r = duration_us - elapsed(now);
    return r < 0 ? 0 : r;
  }
};
