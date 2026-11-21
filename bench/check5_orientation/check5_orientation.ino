// Bench check 5 (docs/hardware.md §9): matrix orientation vs buttons and USB-C.
//
// Draws an arrow pointing at the panel's "top" edge for the current
// M5.Display rotation, plus a single pixel in the top-left corner, and logs
// which of BtnA/B/C/PWR is pressed so the physical keys can be named.
//
// Serial: 0..3 = setRotation(n) and redraw, c = cycle rotations 3 s each.
// Starts at rotation 0.
#include <M5Unified.h>

static int s_rot = 0;

static void draw() {
  M5.Display.setRotation(s_rot);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_BLACK);
  int w = M5.Display.width(), h = M5.Display.height();
  // arrow: shaft from the bottom to the top edge, head at the top
  M5.Display.drawFastVLine(w / 2, 3, h - 4, TFT_WHITE);
  M5.Display.drawFastVLine(w / 2 - 1, 3, h - 4, TFT_WHITE);
  for (int i = 0; i < 5; ++i) M5.Display.drawFastHLine(w / 2 - 1 - i, 1 + i, 2 + 2 * i, TFT_WHITE);
  M5.Display.drawPixel(0, 0, TFT_WHITE);  // top-left marker
  M5.Display.endWrite();
  Serial.printf("rotation %d: %dx%d, arrow points to the panel's top edge; dot = top-left\n", s_rot, w, h);
}

void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.internal_imu = false;
  M5.begin(cfg);
  M5.Display.setBrightness(48);
  draw();
}

void loop() {
  M5.update();
  if (M5.BtnA.wasPressed()) Serial.println("BTN A (KEY1) pressed");
  if (M5.BtnB.wasPressed()) Serial.println("BTN B (KEY2) pressed");
  if (M5.BtnC.wasPressed()) Serial.println("BTN C (KEY3) pressed");
  if (M5.BtnPWR.wasClicked()) Serial.println("BTN PWR clicked");
  while (Serial.available()) {
    int c = Serial.read();
    if (c >= '0' && c <= '3') { s_rot = c - '0'; draw(); }
    else if (c == 'c') for (int r = 0; r < 4; ++r) { s_rot = r; draw(); delay(3000); }
  }
  delay(10);
}
