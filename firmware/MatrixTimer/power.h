// Power: USB detection, battery, the PM1 5 V boost (bars + Bottom2), LVP,
// single-click-reset disable, idle power-off. Never touches the PM1 LDO:
// switching it off clamps the I2C bus (hardware.md §3.5 of bench-results).
#pragma once
#include <M5Unified.h>

#include "config.h"

class Power {
 public:
  void begin() {
    // LVP 3.3 V: reg = (mV - 2000) / 7.81 (hardware.md §2.4)
    M5.In_I2C.writeRegister8(kPm1, 0x08, (uint8_t)((cfg::kLvpMv - 2000) * 100 / 781), 100000);
    // Single click -> event, not reset. Reasserted every boot (bench check 6).
    uint8_t c1 = M5.In_I2C.readRegister8(kPm1, 0x49, 100000);
    M5.In_I2C.writeRegister8(kPm1, 0x49, c1 | 0x01, 100000);
    poll(millis(), true);
  }

  void poll(uint32_t ms, bool force = false) {
    if (!force && ms - lastPoll_ < 1000) return;
    lastPoll_ = ms;
    uint16_t vin = M5.Power.M5pm1.getVBUSVoltage();
    usb_ = vin > cfg::kUsbMv;
    level_ = M5.Power.getBatteryLevel();      // -1 for ~30 s after boot
    charging_ = M5.Power.isCharging();
    vbat_ = M5.Power.M5pm1.getBatteryVoltage();
  }

  bool onUsb() const { return usb_; }
  int level() const { return level_; }     // -1 = unknown
  bool charging() const { return charging_; }
  uint16_t vbat() const { return vbat_; }
  bool low() const { return level_ >= 0 && level_ < cfg::kLowBatteryPct && !usb_; }

  // Returns true when the boost just went off->on (tilt must re-fill).
  bool setBoost(bool on) {
    if (on == boost_ && known_) return false;
    M5.Power.M5pm1.setExtOutput(on);
    bool rose = on && (!boost_ || !known_);
    boost_ = on;
    known_ = true;
    if (rose) delay(20);  // let the Bottom2 3V3 come up before touching 0x68
    return rose;
  }
  bool boost() const { return boost_; }

  void powerOff() { M5.Power.M5pm1.powerOff(); }

 private:
  static constexpr uint8_t kPm1 = 0x6E;
  uint32_t lastPoll_ = 0;
  bool usb_ = true, charging_ = false, boost_ = true, known_ = false;
  int level_ = -1;
  uint16_t vbat_ = 0;
};
