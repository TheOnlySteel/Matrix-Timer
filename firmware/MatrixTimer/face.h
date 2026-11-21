// Face: what the 16x16 matrix shows. A port of tools/mockups/render.py
// (ticks_left, active_fraction, grid, text_frame, duration_frame), so the
// device and the mockups agree. Pure: also built by the host test.
#pragma once
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

// One row per uint16_t, bit 15 = column 0 (left). Logical coordinates:
// row 0 is the top after rotation.
struct Frame {
  uint16_t row[cfg::kH] = {};
  bool operator==(const Frame& o) const { return memcmp(row, o.row, sizeof row) == 0; }
  bool operator!=(const Frame& o) const { return !(*this == o); }
  void set(int r, int c, bool v) {
    if (r < 0 || r >= cfg::kH || c < 0 || c >= cfg::kW) return;
    uint16_t m = 0x8000 >> c;
    if (v) row[r] |= m; else row[r] &= ~m;
  }
  bool get(int r, int c) const { return row[r] & (0x8000 >> c); }
  void fill(bool v) { for (auto& r : row) r = v ? 0xFFFF : 0; }
  int count() const { int n = 0; for (auto r : row) n += __builtin_popcount(r); return n; }
};

namespace face {

// ceil() so a lit LED means time remains; 0 only at exactly zero.
inline int ticksLeft(double remaining_s, double total_s) {
  if (remaining_s <= 0 || total_s <= 0) return 0;
  double t = ceil(cfg::kN * remaining_s / total_s - 1e-9);
  if (t < 1) t = 1;
  return t > cfg::kN ? cfg::kN : (int)t;
}

// How much of the currently ticking LED's own slice is left, in (0, 1].
inline double activeFraction(double remaining_s, double total_s) {
  int left = ticksLeft(remaining_s, total_s);
  if (left == 0) return 0.0;
  double slice = total_s / cfg::kN;
  return (remaining_s - (left - 1) * slice) / slice;
}

// Lit LEDs = time left, ticked off row 0 left->right, then the next row.
// The first lit LED blinks kBlinkOnS on / (period - on) off. t_s < 0 freezes
// it steady (paused). This is the exact-time reference (render.py's grid());
// the device draws gridLeft() through the cycle-aligned smoothing in the app.
inline Frame gridLeft(int left, bool activeOn) {
  Frame f;
  f.fill(true);
  int gone = cfg::kN - left;
  for (int i = 0; i < gone; ++i) f.set(i / cfg::kW, i % cfg::kW, false);
  if (left > 0 && !activeOn) f.set(gone / cfg::kW, gone % cfg::kW, false);
  return f;
}

inline bool blinkOn(double t_s) { return fmod(t_s, cfg::kBlinkPeriodS) < cfg::kBlinkOnS; }

inline Frame grid(double remaining_s, double total_s, double t_s) {
  int left = ticksLeft(remaining_s, total_s);
  bool blinking = t_s >= 0 && total_s / cfg::kN >= cfg::kMinSliceForBlinkS;
  return gridLeft(left, !blinking || blinkOn(t_s));
}

// Cycle-aligned display (Steel, 21 Nov): the shown LED count only changes at
// the start of a blink cycle, so the rhythm never breaks between LEDs or rows.
// In the off phase right before a hand-over, the outgoing active LED and the
// next one go dark together; the next one then blinks on at the cycle start.
// Feed it the block's elapsed time (pauses excluded) so every block starts
// with a full "on". Resyncs at once if the true count moves by more than one
// step or goes up (+5 min).
struct SmoothGrid {
  int shown = -1;
  long cycle = -1;
  void reset() { shown = -1; cycle = -1; }
  Frame frame(double remaining_s, double total_s, double elapsed_s, bool paused) {
    int left = ticksLeft(remaining_s, total_s);
    if (total_s / cfg::kN < cfg::kMinSliceForBlinkS) { shown = left; return gridLeft(left, true); }
    long c = (long)floor(elapsed_s / cfg::kBlinkPeriodS);
    if (shown < 0 || left > shown || shown - left > 1 || (!paused && c != cycle)) shown = left;
    cycle = c;
    if (paused) return gridLeft(shown, true);
    bool on = blinkOn(elapsed_s);
    Frame f = gridLeft(shown, on);
    if (!on && shown > 1) {
      // will the next cycle start hand over? then darken the incoming LED too
      double toNext = (c + 1) * cfg::kBlinkPeriodS - elapsed_s;
      if (ticksLeft(remaining_s - toNext, total_s) < shown) {
        int next = cfg::kN - shown + 1;
        f.set(next / cfg::kW, next % cfg::kW, false);
      }
    }
    return f;
  }
};

// Grid with only the first `lit` LEDs (in tick order from the end) kept:
// used by the cancel wipe.
inline Frame litTail(int lit) {
  Frame f;
  for (int i = cfg::kN - lit; i < cfg::kN; ++i) f.set(i / cfg::kW, i % cfg::kW, true);
  return f;
}

// 3x5 font. Digits and ':' from render.py, plus the letters the settings
// menu needs.
inline const char* glyph(char ch) {
  switch (ch) {
    case '0': return "111101101101111";
    case '1': return "010110010010111";
    case '2': return "111001111100111";
    case '3': return "111001111001111";
    case '4': return "101101111001001";
    case '5': return "111100111001111";
    case '6': return "111100111101111";
    case '7': return "111001010010010";
    case '8': return "111101111101111";
    case '9': return "111101111001111";
    case 'A': return "010101111101101";
    case 'B': return "110101110101110";
    case 'I': return "111010010010111";
    case 'M': return "101111111101101";
    case 'O': return "111101101101111";
    case 'S': return "111100111001111";
    case 'F': return "111100110100100";
    case '-': return "000000111000000";
    default:  return nullptr;
  }
}
inline int glyphW(char ch) { return ch == ':' ? 1 : 3; }

// Draw `s` with each font pixel scaled sx by sy, centred horizontally; y0 < 0
// centres vertically. The gap between glyphs scales with sx (2 px at 2x), so
// two 2x digits are 14 px wide and sit exactly centred; an odd width rounds
// right rather than left (Steel, 21 Nov: digits looked left-skewed).
inline void drawText(Frame& f, const char* s, int sx, int sy, int y0 = -1) {
  int n = strlen(s), total = 0;
  for (int i = 0; i < n; ++i) total += glyphW(s[i]) * sx + (i ? sx : 0);
  int x = (cfg::kW - total + 1) / 2;
  int y = y0 >= 0 ? y0 : (cfg::kH - 5 * sy) / 2;
  for (int i = 0; i < n; ++i) {
    char ch = s[i];
    for (int gy = 0; gy < 5; ++gy)
      for (int gx = 0; gx < glyphW(ch); ++gx) {
        bool on = ch == ':' ? (gy == 1 || gy == 3) : (glyph(ch) && glyph(ch)[gy * 3 + gx] == '1');
        if (on)
          for (int dy = 0; dy < sy; ++dy)
            for (int dx = 0; dx < sx; ++dx) f.set(y + gy * sy + dy, x + gx * sx + dx, true);
      }
    x += glyphW(ch) * sx + sx;
  }
}

inline Frame textFrame(const char* s, int sx, int sy) {
  Frame f;
  drawText(f, s, sx, sy);
  return f;
}

// Under 100 minutes: minutes at 2x. 100 and over: H:MM at 1x wide, 2x tall.
inline Frame durationFrame(int minutes) {
  char buf[8];
  if (minutes < 100) {
    snprintf(buf, sizeof buf, "%d", minutes);
    return textFrame(buf, 2, 2);
  }
  snprintf(buf, sizeof buf, "%d:%02d", minutes / 60, minutes % 60);
  return textFrame(buf, 1, 2);
}

// Peek: remaining as digits. Under a minute: seconds; else whole minutes,
// rounded up so "1" shows until the last minute starts.
inline Frame peekFrame(double remaining_s) {
  if (remaining_s < 60) {
    char buf[4];
    snprintf(buf, sizeof buf, "%d", (int)ceil(remaining_s - 1e-9));
    return textFrame(buf, 2, 2);
  }
  return durationFrame((int)ceil(remaining_s / 60 - 1e-9));
}

// Done: a 2-px-thick checkmark, so the alarm says "finished" instead of
// lighting all 256 (Steel, 21 Nov).
inline Frame checkFrame() {
  static const int8_t pts[][2] = {{8, 2}, {9, 3}, {10, 4}, {11, 5}, {10, 6}, {9, 7}, {8, 8},
                                  {7, 9}, {6, 10}, {5, 11}, {4, 12}, {3, 13}};
  Frame f;
  for (auto& p : pts) { f.set(p[0], p[1], true); f.set(p[0] + 1, p[1], true); }
  return f;
}

// Done: three solid triangles stacked into one, full screen (Steel, 21 Nov:
// the first, 8-row version looked squat). Each triangle is 8 rows tall with a
// 2-px apex, widening by 2 every second row; the bottom row spans all 16.
inline Frame triforceFrame() {
  Frame f;
  auto tri = [&](int top, int cx) {  // cx = left column of the 2-px apex
    for (int j = 0; j < 8; ++j)
      for (int c = cx - j / 2; c <= cx + 1 + j / 2; ++c) f.set(top + j, c, true);
  };
  tri(0, 7);   // upper
  tri(8, 3);   // lower left
  tri(8, 11);  // lower right
  return f;
}

// Settings: item letter on top, value underneath, both 3x5 at 1x.
inline Frame settingsFrame(char item, const char* value) {
  Frame f;
  char s[2] = {item, 0};
  drawText(f, s, 1, 1, 2);
  drawText(f, value, 1, 1, 9);
  for (int c = 0; c < cfg::kW; ++c) f.set(7, c, c % 2 == 0);  // dotted divider
  return f;
}

}  // namespace face
