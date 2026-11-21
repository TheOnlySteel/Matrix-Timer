// Matrix: logical 16x16 frame -> TM1680 via M5GFX, with our own rotation.
//
// The panel is always driven at M5GFX rotation 0, where (bench, hardware.md
// §2.2) panel "up" is the button edge, the USB-C edge is the panel's left and
// nothing is mirrored. Orientation is applied here in software so the mapping
// is known exactly. `up` names the panel edge that content-up points at:
//   0 = panel top    (button edge up)
//   1 = panel right  (USB-C edge down: the cable exits underneath)
//   2 = panel bottom (button edge down)
//   3 = panel left   (USB-C edge up)
#pragma once
#include <M5Unified.h>

#include "face.h"

namespace orient {
enum : uint8_t { kButtonsUp = 0, kUsbDown = 1, kButtonsDown = 2, kUsbUp = 3 };
}

class Matrix {
 public:
  void begin() {
    M5.Display.setRotation(0);
    M5.Display.fillScreen(TFT_BLACK);
    applyDuty(0);
  }

  void setUp(uint8_t up) { up_ = up & 3; }
  uint8_t up() const { return up_; }

  // TM1680 duty n/16 for n = 1..16, 0 = LED_OFF. M5GFX maps setBrightness(b)
  // to duty register (b*16)>>8, i.e. duty (k+1)/16, so b = (n-1)*16 + 8.
  void applyDuty(int n) {
    if (n < 0) n = 0;
    if (n > 16) n = 16;
    if (n == duty_) return;
    duty_ = n;
    M5.Display.setBrightness(n == 0 ? 0 : (n - 1) * 16 + 8);
  }
  int duty() const { return duty_; }

  // Push only when the physical frame changed (32 bytes, ~0.8 ms of bus).
  void show(const Frame& logical) {
    Frame phys = transform(logical);
    shown_ = logical;
    if (havePushed_ && phys == pushed_) return;
    M5.Display.startWrite();
    for (int r = 0; r < cfg::kH; ++r)
      for (int c = 0; c < cfg::kW; ++c) M5.Display.drawPixel(c, r, phys.get(r, c) ? TFT_WHITE : TFT_BLACK);
    M5.Display.endWrite();
    pushed_ = phys;
    havePushed_ = true;
  }
  const Frame& shown() const { return shown_; }

  Frame transform(const Frame& in) const {
    if (up_ == 0) return in;
    Frame out;
    for (int r = 0; r < cfg::kH; ++r)
      for (int c = 0; c < cfg::kW; ++c) {
        if (!in.get(r, c)) continue;
        switch (up_) {
          case 1: out.set(c, 15 - r, true); break;
          case 2: out.set(15 - r, 15 - c, true); break;
          case 3: out.set(15 - c, r, true); break;
        }
      }
    return out;
  }

 private:
  uint8_t up_ = 0;
  int duty_ = -1;
  Frame pushed_, shown_;
  bool havePushed_ = false;
};
