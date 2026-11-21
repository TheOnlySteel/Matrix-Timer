// Bench check 4 (docs/hardware.md §9): map the Bottom2's 10 SK6812 LEDs.
//
// The ESP32-C61 has no RMT, so the bars are driven by SPI2 as a waveform
// generator on G5 (hardware.md §6): 3.2 MHz, 4 SPI bits per LED bit
// (0 -> 1000, 1 -> 1100), GRB order, then >= 80 us low to latch. Uses the
// IDF spi_master directly with DMA: one continuous transaction, and SCLK can
// be left unassigned (-1), which Arduino's SPIClass::begin() refuses.
//
// SAFETY: every channel value is clamped to kMax = 25 (~10 % of 255).
//
// Serial commands:
//   0..9  light only that index (white, kMax) and show the index on the matrix
//   c     cycle 0..9, 4 s each      x  all off      a  all on (flicker check)
//   g     gauge test: indices 0..n lit in turn (shows chain direction)
//   B / b boost on / off (does the bar 5 V survive boost-off on USB?)
//   v     print 5VOUT / VBUS / VBAT from the PM1
//   L     toggle the hands-free show (on at boot), which repeats forever:
//           1. index 0..9 in turn, 3 s each, index digit on the matrix
//           2. all 10 on, matrix "B+" (boost on) 4 s, then "B-" (boost off) 4 s, twice
// Button presses (BtnA/B/C, PWR) are logged at all times.
#include <M5Unified.h>
#include <driver/spi_master.h>

static constexpr int kPin = 5;
static constexpr int kLeds = 10;
static constexpr uint8_t kMax = 25;          // 10 % brightness cap
static constexpr int kLatchBytes = 40;       // 40 * 8 / 3.2 MHz = 100 us low

static spi_device_handle_t s_dev;
static uint8_t* s_buf;                       // DMA-capable
static uint8_t s_rgb[kLeds][3];

static void barsInit() {
  spi_bus_config_t bus = {};
  bus.mosi_io_num = kPin;
  bus.miso_io_num = -1;
  bus.sclk_io_num = -1;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = kLeds * 12 + kLatchBytes * 2;
  esp_err_t e1 = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 3200000;
  dev.mode = 0;
  dev.spics_io_num = -1;
  dev.queue_size = 1;
  esp_err_t e2 = spi_bus_add_device(SPI2_HOST, &dev, &s_dev);
  s_buf = (uint8_t*)heap_caps_malloc(kLeds * 12 + kLatchBytes * 2, MALLOC_CAP_DMA);
  Serial.printf("bars: spi_bus_initialize=%d add_device=%d buf=%p\n", e1, e2, s_buf);
}

// Each LED byte -> 4 SPI bytes (two LED bits per SPI byte).
static void barsShow() {
  uint8_t* p = s_buf;
  for (int i = 0; i < kLatchBytes; ++i) *p++ = 0;  // leading low: clean start
  for (int i = 0; i < kLeds; ++i) {
    const uint8_t grb[3] = {s_rgb[i][1], s_rgb[i][0], s_rgb[i][2]};
    for (uint8_t v : grb) {
      v = v > kMax ? kMax : v;
      for (int b = 7; b >= 0; b -= 2) {
        uint8_t hi = (v >> b) & 1 ? 0xC : 0x8;
        uint8_t lo = (v >> (b - 1)) & 1 ? 0xC : 0x8;
        *p++ = (hi << 4) | lo;
      }
    }
  }
  for (int i = 0; i < kLatchBytes; ++i) *p++ = 0;
  spi_transaction_t t = {};
  t.length = (p - s_buf) * 8;
  t.tx_buffer = s_buf;
  spi_device_polling_transmit(s_dev, &t);
}

static void setAll(uint8_t r, uint8_t g, uint8_t b) {
  for (auto& c : s_rgb) { c[0] = r; c[1] = g; c[2] = b; }
}

static void showLabel(const char* s) {
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(s, 8, 8);
  M5.Display.endWrite();
}

static void only(int i) {
  setAll(0, 0, 0);
  s_rgb[i][0] = s_rgb[i][1] = s_rgb[i][2] = kMax;
  barsShow();
  char l[4];
  snprintf(l, sizeof l, "%d", i);
  showLabel(l);
  Serial.printf("LED index %d on (white %d/255)\n", i, kMax);
}

static void volts(const char* tag) {
  Serial.printf("V [%s] boost=%d 5VOUT=%umV VBUS=%umV VBAT=%umV\n", tag,
                (int)M5.Power.M5pm1.getExtOutput(), M5.Power.M5pm1.get5VoutVoltage(),
                M5.Power.M5pm1.getVBUSVoltage(), M5.Power.M5pm1.getBatteryVoltage());
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;
  M5.begin(cfg);
  M5.Display.setBrightness(48);
  M5.Display.setTextSize(1);
  barsInit();
  setAll(0, 0, 0);
  barsShow();
  showLabel("-");
}

static bool s_show = true;

static void boostSet(bool on) {
  M5.Power.M5pm1.setExtOutput(on);
  delay(50);
  barsShow();  // re-send: LEDs that lost power come back black
  volts(on ? "boost on" : "boost off");
}

// Non-blocking show: one step every call when due.
static void showStep() {
  static uint32_t next = 0;
  static int step = 0;  // 0..9 indices, 10..13 boost test
  if (millis() < next) return;
  if (step < kLeds) {
    only(step);
    next = millis() + 3000;
  } else {
    int k = step - kLeds;  // 0: B+ 1: B- 2: B+ 3: B-
    setAll(kMax, kMax, kMax);
    bool on = (k % 2) == 0;
    boostSet(on);
    showLabel(on ? "B+" : "B-");
    Serial.printf("SHOW boost test: all on, boost %s\n", on ? "ON" : "OFF");
    next = millis() + 4000;
  }
  if (++step >= kLeds + 4) { step = 0; boostSet(true); }
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) Serial.println("BTN A (KEY1) pressed");
  if (M5.BtnB.wasPressed()) Serial.println("BTN B (KEY2) pressed");
  if (M5.BtnC.wasPressed()) Serial.println("BTN C (KEY3) pressed");
  if (M5.BtnPWR.wasClicked()) Serial.println("BTN PWR clicked");
  if (s_show) showStep();
  while (Serial.available()) {
    int c = Serial.read();
    if (c >= '0' && c <= '9') only(c - '0');
    else if (c == 'x') { setAll(0, 0, 0); barsShow(); showLabel("-"); Serial.println("all off"); }
    else if (c == 'a') { setAll(kMax, kMax, kMax); barsShow(); showLabel("A"); Serial.println("all on"); }
    else if (c == 'c') {
      for (int i = 0; i < kLeds; ++i) { only(i); delay(4000); }
      setAll(0, 0, 0); barsShow(); showLabel("-"); Serial.println("cycle done");
    } else if (c == 'g') {
      setAll(0, 0, 0);
      for (int i = 0; i < kLeds; ++i) {
        s_rgb[i][2] = kMax;  // blue, growing from index 0
        barsShow();
        Serial.printf("gauge 0..%d\n", i);
        delay(700);
      }
    } else if (c == 'B') { M5.Power.M5pm1.setExtOutput(true); delay(200); barsShow(); volts("boost on"); }
    else if (c == 'b') { M5.Power.M5pm1.setExtOutput(false); delay(200); barsShow(); volts("boost off"); }
    else if (c == 'v') volts("now");
    else if (c == 'L') { s_show = !s_show; boostSet(true); Serial.printf("show %d\n", s_show); }
  }
  delay(10);
}
