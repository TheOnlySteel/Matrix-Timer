// Bench check 6 (docs/hardware.md §9): power-button behaviour.
//
// Expected PM1 defaults: single click = reset, double click = power off,
// long press = download mode. With SINGLE_RST_DIS (BTN_CFG_1 0x49 bit 0) set,
// a single click should instead arrive as M5.BtnPWR.wasClicked().
//
// Boots print a counter kept in the PM1 RTC RAM (0xA0..), which survives ESP
// resets, plus the ESP reset reason, so a button-triggered reset is visible.
//
// Serial: r = report registers, D = disable single-click reset,
//         E = enable it again (factory behaviour), z = zero the boot counter.
#include <M5Unified.h>

static constexpr uint8_t kPm1 = 0x6E;
static constexpr uint8_t kBtnCfg1 = 0x49, kBtnCfg2 = 0x4A, kBtnStatus = 0x48, kRtcRam = 0xA0;

static uint8_t rd(uint8_t reg) { return M5.In_I2C.readRegister8(kPm1, reg, 100000); }
static bool wr(uint8_t reg, uint8_t v) { return M5.In_I2C.writeRegister8(kPm1, reg, v, 100000); }

static void report(const char* tag) {
  uint8_t c1 = rd(kBtnCfg1), c2 = rd(kBtnCfg2);
  Serial.printf("REG [%s] BTN_CFG_1=0x%02x (DL_LOCK=%d DBL_DLY=%d LONG_DLY=%d CLK_DLY=%d SINGLE_RST_DIS=%d) "
                "BTN_CFG_2=0x%02x (DOUBLE_OFF_DIS=%d) BTN_STATUS=0x%02x\n",
                tag, c1, c1 >> 7, (c1 >> 5) & 3, (c1 >> 3) & 3, (c1 >> 1) & 3, c1 & 1, c2, c2 & 1, rd(kBtnStatus));
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;
  M5.begin(cfg);
  M5.Display.setBrightness(48);
  uint8_t boots = rd(kRtcRam) + 1;
  wr(kRtcRam, boots);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(String(boots % 100), 8, 8);
  delay(1500);  // let the host reopen the port
  Serial.printf("\nBOOT #%u (PM1 RTC RAM counter) esp_reset_reason=%d wake_src=0x%02x\n", boots,
                (int)esp_reset_reason(), M5.Power.M5pm1.getWakeSource());
  report("boot");
}

void loop() {
  M5.update();
  if (M5.BtnPWR.wasClicked()) Serial.println("EVENT BtnPWR.wasClicked()");
  if (M5.BtnPWR.wasHold()) Serial.println("EVENT BtnPWR.wasHold()");
  if (M5.Power.M5pm1.wasPekDoubleClicked()) Serial.println("EVENT PM1 double-click flag");
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'r': report("now"); break;
      case 'D': wr(kBtnCfg1, rd(kBtnCfg1) | 0x01); report("after disable"); break;
      case 'E': wr(kBtnCfg1, rd(kBtnCfg1) & ~0x01); report("after enable"); break;
      case 'z': wr(kRtcRam, 0); Serial.println("counter zeroed"); break;
    }
  }
  delay(10);
}
