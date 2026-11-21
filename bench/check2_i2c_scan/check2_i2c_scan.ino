// Bench check 2 (docs/hardware.md §9): I2C scan with the M-Bus boost on vs off.
//
// Probes every 7-bit address twice: a zero-length WRITE (START, addr+W, STOP)
// and a 1-byte READ. The TM1680 at 0x72 ACKs only writes, so only the write
// probe finds it. Sequence: boost on -> scan, boost off -> scan, boost on -> scan.
// Anything that disappears with the boost off lives on the Bottom2.
//
// Serial: 'r' re-runs the sequence. Runs once automatically 3 s after boot.
#include <M5Unified.h>

static constexpr uint32_t kFreq = 100000;

// M5GFX queues the transaction: start() only fails on a busy bus, and the
// address ACK/NACK is reported by stop(). (= M5.In_I2C.scanID(a))
static bool probeWrite(uint8_t a) {
  return M5.In_I2C.start(a, false, kFreq) && M5.In_I2C.stop();
}

static bool probeRead(uint8_t a) {
  uint8_t b;
  if (!M5.In_I2C.start(a, true, kFreq)) return false;
  bool ok = M5.In_I2C.read(&b, 1, true);
  return M5.In_I2C.stop() && ok;
}

static void scan(const char* label) {
  String w, r;
  int nw = 0, nr = 0;
  for (uint8_t a = 0x08; a < 0x78; ++a) {
    if (probeWrite(a)) { w += String(" 0x") + String(a, HEX); ++nw; }
    if (probeRead(a))  { r += String(" 0x") + String(a, HEX); ++nr; }
  }
  Serial.printf("SCAN %-12s write-probe(%d):%s\n", label, nw, w.c_str());
  Serial.printf("SCAN %-12s read-probe (%d):%s\n", label, nr, r.c_str());
}

static void runSequence() {
  Serial.printf("\n== check2 i2c scan, board=%d, ext(boost)=%d\n",
                (int)M5.getBoard(), (int)M5.Power.M5pm1.getExtOutput());
  M5.Power.M5pm1.setExtOutput(true);
  delay(300);
  scan("boost-on");
  bool ok = M5.Power.M5pm1.setExtOutput(false);
  delay(500);
  Serial.printf("setExtOutput(false) -> %d, readback=%d\n", ok, (int)M5.Power.M5pm1.getExtOutput());
  scan("boost-off");
  M5.Power.M5pm1.setExtOutput(true);
  delay(500);
  scan("boost-on-2");
  Serial.println("== done");
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;  // don't let M5Unified touch either BMI270
  M5.begin(cfg);
  M5.Display.setBrightness(0);
}

void loop() {
  static uint32_t t0 = millis();
  static bool done = false;
  if (!done && millis() - t0 > 3000) { runSequence(); done = true; }
  while (Serial.available()) {
    if (Serial.read() == 'r') runSequence();
  }
  delay(10);
}
