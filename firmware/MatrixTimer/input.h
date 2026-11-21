// Input: A/B/C from the PM1 (via M5.update()), with our own 50 ms lockout per
// button on top of M5Unified's debounce (one B press registered twice on the
// bench). Also accepts simulated presses from the serial console.
#pragma once
#include <M5Unified.h>

#include "config.h"

struct Key {
  bool down = false;
  bool pressed = false;   // edge, this update only
  bool released = false;  // edge, this update only
  uint32_t downAt = 0, upAt = 0;
  bool consumed = false;  // a hold/chord already acted on this press

  uint32_t heldFor(uint32_t ms) const { return down ? ms - downAt : 0; }

  void feed(bool raw, uint32_t ms) {
    pressed = released = false;
    if (raw && !down && ms - upAt >= cfg::kLockoutMs) {
      down = true; downAt = ms; pressed = true; consumed = false;
    } else if (!raw && down && ms - downAt >= cfg::kLockoutMs) {
      down = false; upAt = ms; released = true;
    }
  }
};

class Input {
 public:
  Key a, b, c;
  bool pwrClicked = false;

  void update(uint32_t ms) {
    M5.update();
    a.feed(M5.BtnA.isPressed() || simDown(0, ms), ms);
    b.feed(M5.BtnB.isPressed() || simDown(1, ms), ms);
    c.feed(M5.BtnC.isPressed() || simDown(2, ms), ms);
    pwrClicked = M5.BtnPWR.wasClicked() || simPwr_;
    simPwr_ = false;
    if (a.pressed || b.pressed || c.pressed || pwrClicked) lastActivity_ = ms;
  }

  uint32_t lastActivity() const { return lastActivity_; }

  // Console: hold key k (0..2) for `ms`.
  void simulate(int k, uint32_t holdMs, uint32_t now) { simUntil_[k] = now + holdMs; simFrom_[k] = now; }
  void simulatePower() { simPwr_ = true; }

 private:
  bool simDown(int k, uint32_t ms) const {
    return simUntil_[k] && (int32_t)(ms - simFrom_[k]) >= 0 && (int32_t)(simUntil_[k] - ms) > 0;
  }
  uint32_t simUntil_[3] = {}, simFrom_[3] = {};
  bool simPwr_ = false;
  uint32_t lastActivity_ = 0;
};
