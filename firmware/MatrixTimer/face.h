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
// The first lit LED blinks with a 2.5 s cycle; its on-time runs from 1.5 s
// down to 1 s with the share of its slice still left (>= 1 s on and off).
// t_s < 0 freezes it steady (paused).
inline Frame grid(double remaining_s, double total_s, double t_s) {
  Frame f;
  f.fill(true);
  int left = ticksLeft(remaining_s, total_s);
  int gone = cfg::kN - left;
  for (int i = 0; i < gone; ++i) f.set(i / cfg::kW, i % cfg::kW, false);
  if (t_s >= 0 && left > 0 && total_s / cfg::kN >= cfg::kMinSliceForBlinkS) {
    double on = cfg::kBlinkMinOnS + (cfg::kBlinkMaxOnS - cfg::kBlinkMinOnS) * activeFraction(remaining_s, total_s);
    f.set(gone / cfg::kW, gone % cfg::kW, fmod(t_s, cfg::kBlinkPeriodS) < on);
  }
  return f;
}

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
// centres vertically.
inline void drawText(Frame& f, const char* s, int sx, int sy, int y0 = -1) {
  int n = strlen(s), total = 0;
  for (int i = 0; i < n; ++i) total += glyphW(s[i]) * sx + (i ? 1 : 0);
  int x = (cfg::kW - total) / 2;
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
    x += glyphW(ch) * sx + 1;
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
