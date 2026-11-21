// Tilt: the Core Matrix BMI270's accelerometer -> flat / upright + down edge.
//
// The Bottom2's MPU6886 also answers at 0x68 whenever the 5 V boost is on,
// and every read returns the wired-AND of both chips (hardware.md §5.4).
// Bench-verified workaround: initialise the BMI270 with the boost off, then
// after every boost off->on write 0xFF into 0x0C..0x11 (MPU6886 self-test /
// reserved registers; BMI270 accel data, read-only there) and read only the
// 6-byte accel burst at 0x0C. AND with 0xFF passes the BMI270 data through.
//
// Nothing essential depends on this: on any failure tilt switches itself off
// and the B button / manual orientation still work.
#pragma once
#include <M5Unified.h>
#include <math.h>
#include <utility/imu/BMI270_Class.hpp>

#include "config.h"

// Panel axes (rotation 0): +X toward the panel's right edge, +Y toward the
// panel's bottom edge. kAxis* map raw BMI270 axes onto them; see bench notes in
// docs/firmware.md. Flat matrix-up reads raw z = +1 g on the bench.
#ifndef TILT_PX_FROM_RAW
#define TILT_PX_FROM_RAW(x, y) (y)      // provisional, confirm on the bench
#define TILT_PY_FROM_RAW(x, y) (x)
#endif

class Tilt {
 public:
  enum Pose : uint8_t { kUnknown, kFlat, kUpright };

  // Call with the boost OFF. Returns whether tilt is available at all.
  bool begin() {
    uint8_t id = 0;
    M5.In_I2C.readRegister(kAddr, 0x00, &id, 1, kFreq);
    if (id != 0x24) { available_ = false; return false; }
    m5::BMI270_Class bmi(kAddr, kFreq, &M5.In_I2C);
    available_ = bmi.begin(&M5.In_I2C) != m5::IMU_Base::imu_spec_none;
    if (available_) {
      uint8_t range = 2;
      M5.In_I2C.readRegister(kAddr, 0x41, &range, 1, kFreq);
      lsbPerG_ = 32768.0f / (2 << (range & 3));
    }
    healthy_ = available_;
    return available_;
  }
  bool available() const { return available_; }
  bool usable() const { return available_ && healthy_; }

  // Call after every boost off->on, before the next read.
  void onBoostOn() {
    if (!available_) return;
    uint8_t ff[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    M5.In_I2C.writeRegister(kAddr, 0x0C, ff, sizeof ff, kFreq);
    for (uint8_t r = 0x0C; r <= 0x11; ++r) M5.In_I2C.writeRegister8(kAddr, r, 0xFF, kFreq);
    healthy_ = true;
    badSince_ = 0;
  }
  void onBoostOff() { if (available_) { healthy_ = true; badSince_ = 0; } }

  // ~10 Hz. Updates pose and down edge with dwell/hysteresis.
  void update(uint32_t ms) {
    if (!usable() || ms - lastRead_ < 100) return;
    lastRead_ = ms;
    uint8_t b[6];
    if (!M5.In_I2C.readRegister(kAddr, 0x0C, b, 6, kFreq)) return;
    float x = (int16_t)(b[0] | b[1] << 8) / lsbPerG_;
    float y = (int16_t)(b[2] | b[3] << 8) / lsbPerG_;
    float z = (int16_t)(b[4] | b[5] << 8) / lsbPerG_;
    raw_[0] = x; raw_[1] = y; raw_[2] = z;
    float mag = sqrtf(x * x + y * y + z * z);
    // implausible for 3 s straight -> give up until the next boost change
    if (mag < 0.5f || mag > 1.5f) {
      if (!badSince_) badSince_ = ms ? ms : 1;
      if (ms - badSince_ > 3000) { healthy_ = false; pose_ = kUnknown; }
      return;
    }
    badSince_ = 0;

    float px = TILT_PX_FROM_RAW(x, y), py = TILT_PY_FROM_RAW(x, y);
    float inPlane = sqrtf(px * px + py * py);
    Pose p = fabsf(z) > cfg::kFlatG ? kFlat : (inPlane > cfg::kFlatG ? kUpright : kUnknown);
    if (p != candPose_) { candPose_ = p; candPoseSince_ = ms; }
    uint32_t dwell = p == kFlat ? cfg::kFlatDwellMs : cfg::kUprightDwellMs;
    if (p != kUnknown && ms - candPoseSince_ >= dwell) pose_ = p;

    if (p == kUpright) {
      // the accelerometer reads +1 g pointing UP, so the down edge is -(px,py)
      float dx = -px, dy = -py;
      uint8_t down = fabsf(dx) > fabsf(dy) ? (dx > 0 ? 1 : 3) : (dy > 0 ? 2 : 0);  // 0 top 1 right 2 bottom 3 left
      if (down != candDown_) { candDown_ = down; candDownSince_ = ms; }
      if (ms - candDownSince_ >= cfg::kRotateDwellMs) downEdge_ = down;
    }
  }

  Pose pose() const { return usable() ? pose_ : kUnknown; }
  // Panel edge that faces down (0 top, 1 right, 2 bottom, 3 left), or 255.
  uint8_t downEdge() const { return usable() ? downEdge_ : 255; }
  // Matrix `up` (see matrix.h) for the current down edge.
  uint8_t upForDown() const { return downEdge_ == 255 ? 255 : (downEdge_ + 2) & 3; }
  // Candidate down edge held for at least `ms` (for flip-to-repeat).
  bool downHeld(uint8_t edge, uint32_t ms, uint32_t now) const {
    return usable() && candPose_ == kUpright && candDown_ == edge && now - candDownSince_ >= ms;
  }
  const float* raw() const { return raw_; }

 private:
  static constexpr uint8_t kAddr = 0x68;
  static constexpr uint32_t kFreq = 400000;
  bool available_ = false, healthy_ = false;
  float lsbPerG_ = 4096.0f;
  float raw_[3] = {};
  uint32_t lastRead_ = 0, badSince_ = 0;
  Pose pose_ = kUnknown, candPose_ = kUnknown;
  uint32_t candPoseSince_ = 0, candDownSince_ = 0;
  uint8_t downEdge_ = 255, candDown_ = 255;
};
