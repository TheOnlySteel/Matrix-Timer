// Host tests for the pure modules (Clock, Face). Build: make test
#include <assert.h>
#include <stdio.h>

#include "../MatrixTimer/clock.h"
#include "../MatrixTimer/face.h"

static int fails = 0;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); ++fails; } \
  } while (0)

int main() {
  using namespace face;
  // ticksLeft: ceil, a lit LED means time remains
  CHECK(ticksLeft(1500, 1500) == 256);
  CHECK(ticksLeft(0, 1500) == 0);
  CHECK(ticksLeft(0.001, 1500) == 1);
  CHECK(ticksLeft(1500.0 / 256, 1500) == 1);          // exactly one slice left
  CHECK(ticksLeft(1500.0 / 256 + 0.01, 1500) == 2);
  CHECK(ticksLeft(750, 1500) == 128);
  CHECK(ticksLeft(2000, 1500) == 256);                // clamp

  // activeFraction in (0,1]
  double slice = 1500.0 / 256;
  CHECK(fabs(activeFraction(slice * 10.5, 1500) - 0.5) < 1e-6);
  CHECK(fabs(activeFraction(slice * 10, 1500) - 1.0) < 1e-6);

  // grid: count of lit LEDs, active LED blink
  Frame f = grid(750, 1500, -1);
  CHECK(f.count() == 128);
  CHECK(!f.get(0, 0) && !f.get(7, 15) && f.get(8, 0) && f.get(15, 15));
  // active LED (row 8 col 0): 1 s on, 0.5 s off
  double rem = slice * 127.5;   // 128 left
  CHECK(grid(rem, 1500, 9.9).get(8, 0) == true);     // 9.9 mod 1.5 = 0.9 < 1.0
  CHECK(grid(rem, 1500, 10.2).get(8, 0) == false);   // 1.2

  // SmoothGrid: the shown count changes only at cycle starts, never mid-cycle
  {
    SmoothGrid sg;
    double total = 1500;   // slice 5.86 s
    int changes_mid = 0, prevShown = -1;
    bool prevOn = true;
    int glitches = 0;      // on->off->on faster than the 0.5 s off phase
    int pairMiss = 0, pairs = 0;
    double lastToggle = 0;
    for (double t = 0; t < 120; t += 0.01) {
      Frame f = sg.frame(total - t, total, t, false);
      int shown = sg.shown;
      double phase = fmod(t, 1.5);
      if (prevShown >= 0 && shown != prevShown && phase > 0.011) ++changes_mid;
      bool on = f.get((256 - shown) / 16, (256 - shown) % 16);
      if (on != prevOn) { if (t - lastToggle < 0.49 && t > 0.02) ++glitches; lastToggle = t; prevOn = on; }
      // off phase just before a hand-over: the incoming LED is dark as well
      if (!on && shown > 1) {
        double toNext = (floor(t / 1.5) + 1) * 1.5 - t;
        int idx = 256 - shown + 1;
        bool handover = ticksLeft(total - t - toNext, total) < shown;
        if (handover != !f.get(idx / 16, idx % 16)) ++pairMiss;
        if (handover) ++pairs;
      }
      prevShown = shown;
    }
    CHECK(changes_mid == 0);
    CHECK(glitches == 0);
    CHECK(pairMiss == 0);
    CHECK(pairs > 0);
    // lags the true count by at most one LED
    sg.frame(total - 119.0, total, 119.0, false);
    CHECK(sg.shown - ticksLeft(total - 119.0, total) <= 1);
    // +5 min resyncs upward at once
    sg.frame(total + 300 - 119.5, total + 300, 119.5, false);
    CHECK(sg.shown == ticksLeft(total + 300 - 119.5, total + 300));
  }
  // short blocks: steady
  CHECK(grid(60 * 0.5 + 0.1, 60, 10.9).count() == ticksLeft(30.1, 60));

  // digits
  Frame d = durationFrame(25);   // "25" at 2x: 14 px wide, 10 tall, centred
  CHECK(d.count() > 0);
  int minc = 16, maxc = -1, minr = 16, maxr = -1;
  for (int r = 0; r < 16; ++r)
    for (int c = 0; c < 16; ++c)
      if (d.get(r, c)) { minc = c < minc ? c : minc; maxc = c > maxc ? c : maxc; minr = r < minr ? r : minr; maxr = r > maxr ? r : maxr; }
  CHECK(maxc - minc + 1 == 14);
  CHECK(minc == 1 && maxc == 14);   // exactly centred
  CHECK(maxr - minr + 1 == 12);
  CHECK(minr == 2 && maxr == 13);
  // triforce stays off the outer ring
  Frame tf = triforceFrame();
  for (int k = 0; k < 16; ++k) CHECK(!tf.get(0, k) && !tf.get(15, k) && !tf.get(k, 0) && !tf.get(k, 15));
  Frame hm = durationFrame(105);  // "1:45"
  CHECK(hm.count() > 0);
  CHECK(peekFrame(59.2) == textFrame("60", 2, 2));
  CHECK(peekFrame(30.5) == textFrame("31", 2, 2));
  CHECK(peekFrame(61) == durationFrame(2));

  // Clock: computed, pause, extend, restore
  Clock k;
  k.start(0, 1000000000LL);                 // 1000 s
  CHECK(k.remaining(400000000LL) == 600000000LL);
  k.pause(400000000LL);
  CHECK(k.remaining(900000000LL) == 600000000LL);
  k.resume(900000000LL);
  CHECK(k.remaining(1000000000LL) == 500000000LL);
  k.extend(300000000LL);
  CHECK(k.remaining(1000000000LL) == 800000000LL);
  CHECK(k.remaining(5000000000LL) == 0);
  Clock r;
  r.restore(123, 1000, 400, true);
  CHECK(r.paused && r.remaining(99999) == 400);
  r.resume(99999);
  CHECK(r.remaining(99999 + 100) == 300);

  printf(fails ? "%d FAILED\n" : "all host tests passed\n", fails);
  return fails != 0;
}
