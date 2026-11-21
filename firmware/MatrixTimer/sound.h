// Sound: MLT-8530 passive buzzer. Power enable on IOE1 IO2, PWM on IOE1 IO11
// (PWM channel 3), ~2.7 kHz resonance (design.md §5.5). Non-blocking chirps.
#pragma once
#include <M5Unified.h>
#include <utility/M5IOE1_Class.hpp>

#include "config.h"

class Sound {
 public:
  void begin() {
    auto& io = M5.getIOExpander(0);
    io.setHighImpedance(kEn, false);
    io.setDirection(kEn, true);
    io.digitalWrite(kEn, false);
    // The IOE1 only drives a PWM channel onto its pin when that pin is an
    // output (the standalone M5IOE1 library does this in setPwmDuty12bit;
    // M5Unified's M5IOE1_Class does not). IO11 = 0-based gpio11 = 10.
    io.setHighImpedance(kPwm, false);
    io.setDirection(kPwm, true);
    ioe().setPwmFrequency(cfg::kBuzzerHz);
    tone(false);
  }

  void setEnabled(bool on) { enabled_ = on; if (!on) stop(); }

  // n short chirps: 80 ms on, 120 ms off.
  void chirps(int n) {
    if (!enabled_) return;
    remaining_ = n * 2;
    next_ = 0;
  }
  void stop() { remaining_ = 0; tone(false); }
  bool enabled() const { return enabled_; }

  void update(uint32_t ms) {
    if (remaining_ <= 0 || (int32_t)(ms - next_) < 0) return;
    bool on = remaining_ % 2 == 0;
    tone(on);
    next_ = ms + (on ? 80 : 120);
    --remaining_;
  }

 private:
  // IOE1 pins are 0-based in M5Unified: gpio2 (IO2) == 1. A raw "2" is IO3.
  static constexpr uint8_t kEn = m5::M5IOE1_Class::gpio2;
  static constexpr uint8_t kPwm = m5::M5IOE1_Class::gpio11;
  static m5::M5IOE1_Class& ioe() { return static_cast<m5::M5IOE1_Class&>(M5.getIOExpander(0)); }
  void tone(bool on) {
    if (on == toneOn_) return;
    toneOn_ = on;
    M5.getIOExpander(0).digitalWrite(kEn, on);  // buzzer power only while sounding
    ioe().setPwmDutyPercent(m5::M5IOE1_Class::pwm_ch3, on ? 50 : 0, m5::pwm_polarity_t::normal, on);
  }

  bool enabled_ = true, toneOn_ = true;
  int remaining_ = 0;
  uint32_t next_ = 0;
};
