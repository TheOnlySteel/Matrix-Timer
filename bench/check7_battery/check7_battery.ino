// Bench check 7 (docs/hardware.md §9): battery voltage and charging status.
//
// Prints once a second: M5.Power battery mV / level / isCharging / charge
// state, the PM1 ADC rails (VBAT, VIN = USB, 5VOUT), the PM1 power source and
// the raw CHG_STAT input (IOE1 G8, low = charging).
//
// Off-USB evidence: every 5 s a compact record goes into a RAM ring buffer
// (it survives USB unplug/replug as long as the ESP keeps running on the
// battery). Serial 'P' prints the ring. 'c' clears it.
//
// Read-only: never calls setChargeCurrent() (650 mA would be 1.3 C for the
// Bottom2's 500 mAh cell) and never changes the charger.
#include <M5Unified.h>

struct Rec { uint32_t t; uint16_t vbat, vin; int8_t chg, chgStat, level; };
static Rec s_ring[256];
static int s_n = 0;

static void dumpRing() {
  Serial.printf("RING %d records (t s, VBAT mV, VIN mV, isCharging, CHG_STAT, level%%)\n", s_n);
  for (int i = 0; i < s_n; ++i)
    Serial.printf("REC t=%lus VBAT=%u VIN=%u isCharging=%d CHG_STAT=%d level=%d\n", (unsigned long)s_ring[i].t,
                  s_ring[i].vbat, s_ring[i].vin, s_ring[i].chg, s_ring[i].chgStat, s_ring[i].level);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;
  M5.begin(cfg);
  M5.Display.setBrightness(32);
}

void loop() {
  static uint32_t last = 0;
  M5.update();
  static uint32_t lastRec = 0;
  if (millis() - lastRec >= 5000 && s_n < 256) {
    lastRec = millis();
    s_ring[s_n++] = {millis() / 1000, M5.Power.M5pm1.getBatteryVoltage(), M5.Power.M5pm1.getVBUSVoltage(),
                     (int8_t)M5.Power.isCharging(), (int8_t)M5.getIOExpander(0).digitalRead(8),
                     (int8_t)M5.Power.getBatteryLevel()};
    M5.Display.setBrightness(32);
    M5.Display.setTextDatum(middle_center);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.drawString(String(s_n % 100), 8, 8);  // record count, so a reset is visible
  }
  while (Serial.available()) {
    int c = Serial.read();
    if (c == 'P') dumpRing();
    else if (c == 'c') s_n = 0;
  }
  if (millis() - last >= 1000) {
    last = millis();
    m5::M5PM1_Class::pwr_src_t src{};
    bool srcOk = M5.Power.M5pm1.getPowerSource(&src);
    Serial.printf("BAT t=%lus level=%d%% batt=%dmV isCharging=%d chargeState=%d | PM1 VBAT=%umV VIN=%umV 5VOUT=%umV "
                  "src=%d(%d) ext=%d | CHG_STAT(IOE1 G8)=%d\n",
                  (unsigned long)(millis() / 1000), (int)M5.Power.getBatteryLevel(),
                  (int)M5.Power.getBatteryVoltage(), (int)M5.Power.isCharging(),
                  (int)M5.Power.getChargeState(), M5.Power.M5pm1.getBatteryVoltage(),
                  M5.Power.M5pm1.getVBUSVoltage(), M5.Power.M5pm1.get5VoutVoltage(), (int)src, srcOk,
                  (int)M5.Power.M5pm1.getExtOutput(), (int)M5.getIOExpander(0).digitalRead(8));
  }
  delay(10);
}
