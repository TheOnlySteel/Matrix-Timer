// Bench check 3 (docs/hardware.md §9): where is the Bottom2's BMI270, and does
// it collide with the CoreMatrix's BMI270 at 0x68?
//
// Three independent tests, each run with the M-Bus boost on and off
// (boost off = Bottom2 3V3 unpowered, so only the CoreMatrix IMU remains):
//   1. CHIP_ID (0x00) at 0x68 and 0x69: 0x24 = BMI270.
//   2. SENSORTIME (0x18..0x1A, 24-bit, 39.0625 us/tick): one chip gives a
//      strictly increasing count. Two chips on one address give the wired-AND
//      of two free-running counters: it jumps backwards and stalls.
//   3. Accelerometer magnitude: one chip reads ~1.00 g at rest in any pose.
//      Wired-AND of two chips' data is not gravity-consistent.
//   Plus INTERNAL_STATUS (0x21): after the boost is cycled the Bottom2's chip
//   restarts unconfigured (0x00); AND-ed with a configured chip (0x01) it reads 0.
//
// Serial commands (one char): a = full automatic sequence, i = init BMI270 at
// 0x68 (+0x69), 1 / 0 = boost on / off, p = probe report, s = stream on/off (10 Hz),
// w = CHIP_ID / WHO_AM_I reads at 0x68, d = hex dump of 0x68 registers,
// m = identify the second chip by a write-then-read trick (see identify()),
// t = 120 s motion test alternating boost off/on every 4 s,
// h = 60 s test of the "transparent MPU6886" workaround (see transparentTest()).
//
// Do NOT try to isolate the Bottom2 chip by switching off the PM1 LDO
// (3V3_L1, which powers the CoreMatrix BMI270): the unpowered BMI270 clamps
// SDA/SCL, the PM1 becomes unreachable, and only a PM1 power cycle recovers.
#include <M5Unified.h>
#include <utility/imu/BMI270_Class.hpp>

static constexpr uint32_t kFreq = 400000;
static bool streaming = false;
// ACC_RANGE (0x41) as read right after init. Re-reading 0x41 later is not
// safe with the boost on: it ANDs with the MPU6886's TEMP_OUT_L.
static uint8_t s_range[2] = {2, 2};  // [0x68, 0x69], 2 = +-8 g (BMI270 default)

static uint8_t rd8(uint8_t a, uint8_t reg, bool* ok = nullptr) {
  uint8_t v = 0;
  bool r = M5.In_I2C.readRegister(a, reg, &v, 1, kFreq);
  if (ok) *ok = r;
  return v;
}

static bool present(uint8_t a) { return M5.In_I2C.scanID(a, 100000); }

static void initAt(uint8_t a) {
  m5::BMI270_Class bmi(a, kFreq, &M5.In_I2C);
  auto spec = bmi.begin(&M5.In_I2C);
  if (spec) s_range[a & 1] = rd8(a, 0x41) & 3;
  Serial.printf("INIT 0x%02x: begin -> spec=%d  CHIP_ID=0x%02x INTERNAL_STATUS=0x%02x ACC_RANGE=%d PWR_CTRL=0x%02x\n",
                a, (int)spec, rd8(a, 0x00), rd8(a, 0x21), rd8(a, 0x41), rd8(a, 0x7D));
}

static void boost(bool on) {
  bool ok = M5.Power.M5pm1.setExtOutput(on);
  delay(on ? 300 : 300);
  Serial.printf("BOOST %s: set=%d readback=%d 5VOUT=%umV\n", on ? "ON " : "OFF", ok,
                (int)M5.Power.M5pm1.getExtOutput(), M5.Power.M5pm1.get5VoutVoltage());
}

static void whoami() {
  Serial.printf("WHO 0x68: reg00(BMI270 CHIP_ID,0x24)=0x%02x reg75(MPU6886 WHO_AM_I,0x19)=0x%02x reg6B=0x%02x ack=%d | 0x69 ack=%d\n",
                rd8(0x68, 0x00), rd8(0x68, 0x75), rd8(0x68, 0x6B), (int)present(0x68), (int)present(0x69));
}

static void dump() {
  uint8_t b[128];
  bool ok = M5.In_I2C.readRegister(0x68, 0x00, b, 128, kFreq);
  Serial.printf("DUMP 0x68 boost=%d ok=%d\n", (int)M5.Power.M5pm1.getExtOutput(), ok);
  for (int r = 0; r < 128; r += 16) {
    Serial.printf("  %02x:", r);
    for (int i = 0; i < 16; ++i) Serial.printf(" %02x", b[r + i]);
    Serial.println();
  }
}

static uint32_t sensorTime(uint8_t a) {
  uint8_t b[3] = {0, 0, 0};
  M5.In_I2C.readRegister(a, 0x18, b, 3, kFreq);
  return b[0] | (b[1] << 8) | (b[2] << 16);
}

static void probe(const char* label) {
  Serial.printf("PROBE [%s] boost=%d\n", label, (int)M5.Power.M5pm1.getExtOutput());
  for (uint8_t a : {0x68, 0x69}) {
    bool ok;
    uint8_t id = rd8(a, 0x00, &ok);
    Serial.printf("  0x%02x: ack=%d CHIP_ID=0x%02x INTERNAL_STATUS=0x%02x PWR_CTRL=0x%02x ERR=0x%02x\n",
                  a, (int)present(a), ok ? id : 0, rd8(a, 0x21), rd8(a, 0x7D), rd8(a, 0x02));
  }
  // 40 sensortime reads, ~2 ms apart: count backwards steps / stalls.
  uint32_t prev = sensorTime(0x68), first = prev;
  int back = 0, stall = 0;
  String seq;
  for (int i = 0; i < 40; ++i) {
    delay(2);
    uint32_t t = sensorTime(0x68);
    if (t < prev) ++back;
    else if (t == prev) ++stall;
    if (i < 12) seq += String(" ") + String(t);
    prev = t;
  }
  Serial.printf("  SENSORTIME 0x68: backwards=%d stalls=%d of 40, span=%ld ticks (expect ~%d) seq:%s\n",
                back, stall, (long)(prev - first), (int)(40 * 2.25e-3 / 39.0625e-6), seq.c_str());
}

static void sample() {
  for (uint8_t a : {0x68, 0x69}) {
    if (!present(a)) continue;
    uint8_t b[6];
    if (!M5.In_I2C.readRegister(a, 0x0C, b, 6, kFreq)) continue;
    int16_t x = b[0] | (b[1] << 8), y = b[2] | (b[3] << 8), z = b[4] | (b[5] << 8);
    float lsb = 32768.0f / (2 << s_range[a & 1]);  // range code 0..3 = 2/4/8/16 g
    float gx = x / lsb, gy = y / lsb, gz = z / lsb;
    Serial.printf("ACC 0x%02x boost=%d raw=%6d %6d %6d  g=%+.3f %+.3f %+.3f |g|=%.3f\n", a,
                  (int)M5.Power.M5pm1.getExtOutput(), x, y, z, gx, gy, gz,
                  sqrtf(gx * gx + gy * gy + gz * gz));
  }
}

static void streamFor(uint32_t ms) {
  uint32_t t0 = millis();
  while (millis() - t0 < ms) { sample(); delay(100); }
}

// Identify the chip that shares 0x68 without unpowering anything.
// BMI270 0x72 / 0x75 are OFFSET_1 / OFFSET_4: plain R/W bytes, default 0.
// MPU6886 0x72 is FIFO_COUNTH (read-only, 0) and 0x75 is WHO_AM_I (read-only, 0x19).
// Write 0xFF to both (the write reaches every chip on 0x68), read back the
// wired-AND, then restore 0:
//   readback 0x72=0xFF 0x75=0xFF -> every chip on 0x68 is a BMI270
//   readback 0x72=0x00 0x75=0x19 -> the second chip is an MPU6886
static void identify() {
  Serial.printf("IDENT boost=%d (want 1: Bottom2 powered)\n", (int)M5.Power.M5pm1.getExtOutput());
  uint8_t before72 = rd8(0x68, 0x72), before75 = rd8(0x68, 0x75);
  M5.In_I2C.writeRegister8(0x68, 0x72, 0xFF, kFreq);
  M5.In_I2C.writeRegister8(0x68, 0x75, 0xFF, kFreq);
  delay(2);
  uint8_t r72 = rd8(0x68, 0x72), r75 = rd8(0x68, 0x75);
  M5.In_I2C.writeRegister8(0x68, 0x72, before72, kFreq);
  M5.In_I2C.writeRegister8(0x68, 0x75, 0x00, kFreq);
  Serial.printf("IDENT 0x68 before: r72=0x%02x r75=0x%02x | after writing 0xFF: r72=0x%02x r75=0x%02x -> %s\n",
                before72, before75, r72, r75,
                (r72 == 0xFF && r75 == 0xFF) ? "all BMI270"
                : (r75 == 0x19) ? "second chip looks like MPU6886 (WHO_AM_I 0x19)"
                : "unrecognised");
}
// Motion test: init the CoreMatrix BMI270 with the boost off, then alternate
// 4 s boost-off / 4 s boost-on for `secs`, streaming at 10 Hz, while a person
// tilts the device and lays it flat. The BMI270 config survives boost cycling
// (it is powered from the PM1 LDO, not the boost).
static void motionTest(uint32_t secs) {
  Serial.printf("\n== check3 motion test, %lus\n", (unsigned long)secs);
  boost(false);
  initAt(0x68);
  uint32_t t0 = millis();
  bool on = false;
  while (millis() - t0 < secs * 1000) {
    streamFor(4000);
    on = !on;
    boost(on);
  }
  boost(true);
  Serial.println("== motion done");
}

// Workaround test: make the MPU6886 "transparent" for BMI270 accel reads.
// A read of 0x68 returns BMI270 AND MPU6886 for the same register numbers.
// BMI270 accel data is 0x0C..0x11 (read-only there). On the MPU6886,
// 0x0D..0x0F are SELF_TEST_X/Y/Z_ACCEL (R/W) and 0x0C / 0x10 / 0x11 are
// reserved. Write 0xFF to all six (BMI270 ignores writes to its data regs):
// if the MPU6886 keeps 0xFF there, the AND passes the BMI270 data through.
// Must be redone after every boost-on (the MPU6886 powers up fresh).
static void transparentTest(uint32_t secs) {
  Serial.printf("\n== check3 transparent-MPU test, %lus\n", (unsigned long)secs);
  boost(false);
  initAt(0x68);           // BMI270 alone: config upload never reaches the MPU6886
  streamFor(1500);        // reference
  boost(true);
  delay(100);
  uint8_t ff[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  bool ok = M5.In_I2C.writeRegister(0x68, 0x0C, ff, 6, kFreq);
  for (uint8_t r = 0x0C; r <= 0x11; ++r) M5.In_I2C.writeRegister8(0x68, r, 0xFF, kFreq);
  Serial.printf("wrote 0xFF to 0x0C..0x11 (burst ok=%d)\n", ok);
  M5.Display.setBrightness(48);  // "GO" on the matrix while recording
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString("GO", 8, 8);
  streamFor(secs * 1000);
  M5.Display.fillScreen(TFT_BLACK);
  Serial.println("== transparent done");
}

static void autoSequence() {
  Serial.println("\n== check3 auto sequence");
  boost(true);
  whoami();
  initAt(0x68);
  initAt(0x69);
  probe("boost on, after init");
  streamFor(2000);
  boost(false);
  whoami();
  initAt(0x68);
  probe("boost off, after init");
  streamFor(2000);
  boost(true);
  probe("boost back on, BEFORE re-init");
  streamFor(1000);
  initAt(0x68);
  probe("boost on, after re-init");
  streamFor(2000);
  Serial.println("== auto done");
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;  // we drive both addresses by hand
  M5.begin(cfg);
  M5.Display.setBrightness(0);
}

void loop() {
  while (Serial.available()) {
    switch (Serial.read()) {
      case 'a': autoSequence(); break;
      case 'i': initAt(0x68); initAt(0x69); break;
      case '1': boost(true); break;
      case '0': boost(false); break;
      case 'p': probe("manual"); break;
      case 'w': whoami(); break;
      case 'd': dump(); break;
      case 'm': identify(); break;
      case 't': motionTest(120); break;
      case 'h': transparentTest(60); break;
      case 'H': transparentTest(420); break;
      case 's': streaming = !streaming; Serial.printf("stream %d\n", streaming); break;
    }
  }
  if (streaming) { sample(); delay(100); } else delay(10);
}
