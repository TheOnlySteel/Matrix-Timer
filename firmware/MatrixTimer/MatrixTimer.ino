// Matrix-Timer v1: a row-by-row work timer for the M5Stack CoreMatrix
// (ESP32-C61) on an M5GO Bottom2. Behaviour: docs/design.md. Hardware:
// docs/hardware.md. Notes and console: docs/firmware.md.
//
// One loop, no RTOS tasks of our own. Modules are header-only (one TU).
#include <M5Unified.h>

#include "bars.h"
#include "clock.h"
#include "config.h"
#include "face.h"
#include "input.h"
#include "matrix.h"
#include "persist.h"
#include "power.h"
#include "sound.h"
#include "tilt.h"

enum class St : uint8_t { Set, Running, Paused, Done, Settings, Cancelling };
static const char* stName(St s) {
  switch (s) {
    case St::Set: return "SET";
    case St::Running: return "RUNNING";
    case St::Paused: return "PAUSED";
    case St::Done: return "DONE";
    case St::Settings: return "SETTINGS";
    case St::Cancelling: return "CANCELLING";
  }
  return "?";
}

static Matrix matrix;
static Bars bars;
static Sound sound;
static Power power;
static Tilt tilt;
static Input input;
static Clock clk;
static persist::Settings settings;

static St st = St::Set;
static int minutes = cfg::kDefaultMinutes;  // Set's selected duration
static int64_t lastDurationUs = 0;           // for "repeat" from Done
static int64_t warpUs = 0;                   // console fast-forward (testing)
static bool matrixOn = true;
static bool pausedByFlat = false;
static uint32_t peekUntil = 0;
static uint32_t doneAt = 0, lastChirp = 0;
static uint8_t doneDown = 255;
static int settingsItem = 0;
static uint32_t cancelAt = 0;
static int cancelFrom = 0;
static int lastSavedTicks = -1;
static bool chord = false;          // A+C chord in progress (Set)
static int minutesBeforeChord = 0;
static uint32_t lastRepeatA = 0, lastRepeatC = 0;
static Tilt::Pose lastPose = Tilt::kUnknown;
static face::SmoothGrid smooth;
static uint32_t lastBarsMs = 0;

static int64_t nowUs() { return esp_timer_get_time() + warpUs; }

// ---------------------------------------------------------------- helpers
static int stepUp(int m) { return m >= cfg::kMaxMinutes ? cfg::kMinMinutes : m < 10 ? m + 1 : m < 60 ? m + 5 : m + 15; }
static int stepDown(int m) { return m <= cfg::kMinMinutes ? cfg::kMaxMinutes : m <= 10 ? m - 1 : m <= 60 ? m - 5 : m - 15; }

static void saveSession() {
  persist::State s = st == St::Running ? persist::kRunning
                   : st == St::Paused  ? persist::kPaused
                   : st == St::Done    ? persist::kDone
                                       : persist::kNone;
  int64_t now = nowUs();
  uint32_t dur = (uint32_t)((s == persist::kDone ? lastDurationUs : clk.duration_us) / 1000000);
  persist::save(s, pausedByFlat ? 1 : 0, dur ? dur : 1, (uint32_t)(clk.remaining(now) / 1000));
}

static float barCap() { return settings.barLevel == 0 ? 0.05f : settings.barLevel == 1 ? 0.10f : 0.25f; }

static void applySettings() {
  sound.setEnabled(settings.sound);
  bars.setCap(barCap());
  if (settings.orientation > 0) { matrix.setUp(settings.orientation - 1); bars.setUp(settings.orientation - 1); }
}

static void enter(St s) {
  St from = st;
  st = s;
  uint32_t ms = millis();
  if (s == St::Done) {
    doneAt = ms;
    lastChirp = ms;
    doneDown = tilt.downEdge();
    sound.chirps(3);
  }
  if (from == St::Done && s != St::Done) sound.stop();
  if (s == St::Set) { clk.cancel(); pausedByFlat = false; }
  saveSession();
  Serial.printf("STATE %s -> %s\n", stName(from), stName(s));
}

static void startBlock(int64_t durationUs) {
  clk.start(nowUs(), durationUs);
  lastDurationUs = durationUs;
  lastSavedTicks = -1;
  pausedByFlat = false;
  peekUntil = 0;
  smooth.reset();
  enter(St::Running);
}

static void boostPolicy() {
  bool want = settings.bars == 1 || (settings.bars == 0 && power.onUsb());
  if (!want && power.boost()) bars.blackout();
  bool was = power.boost();
  if (power.setBoost(want)) tilt.onBoostOn();
  else if (was && !power.boost()) tilt.onBoostOff();
}

static void orientationPolicy() {
  if (settings.orientation != 0) return;
  if (tilt.pose() != Tilt::kUpright) return;
  uint8_t up = tilt.upForDown();
  if (up != 255 && up != matrix.up()) {
    matrix.setUp(up);
    bars.setUp(up);
    Serial.printf("ORIENT up=%u (down edge %u)\n", up, tilt.downEdge());
  }
}

// ---------------------------------------------------------------- states
static void handleSet(uint32_t ms) {
  Key &a = input.a, &b = input.b, &c = input.c;
  if ((a.pressed && c.down) || (c.pressed && a.down)) {
    chord = true;
    minutes = minutesBeforeChord;  // undo the step the first key made
  }
  if (chord) {
    if (a.down && c.down && a.heldFor(ms) >= cfg::kChordSettingsMs && c.heldFor(ms) >= cfg::kChordSettingsMs) {
      a.consumed = c.consumed = true;
      chord = false;
      settingsItem = 0;
      enter(St::Settings);
    }
    if (!a.down && !c.down) chord = false;
    return;
  }
  auto repeat = [&](Key& k, uint32_t& last, int dir) {
    if (k.pressed) {
      minutes = dir > 0 ? stepUp(minutes) : stepDown(minutes);
      last = ms;
    } else if (k.down && k.heldFor(ms) >= cfg::kRepeatDelayMs) {
      uint32_t every = k.heldFor(ms) >= cfg::kRepeatFastAfterMs ? cfg::kRepeatFastMs : cfg::kRepeatSlowMs;
      if (ms - last >= every) { minutes = dir > 0 ? stepUp(minutes) : stepDown(minutes); last = ms; }
    }
  };
  repeat(a, lastRepeatA, -1);
  repeat(c, lastRepeatC, +1);
  if (!a.down && !c.down) minutesBeforeChord = minutes;  // value to restore if a chord starts
  if (b.released && !b.consumed) {
    settings.minutes = minutes;
    persist::saveSettings(settings);
    startBlock((int64_t)minutes * 60 * 1000000);
  }
}

static void handleRunPause(uint32_t ms, int64_t now) {
  Key &a = input.a, &b = input.b, &c = input.c;
  if (a.pressed) peekUntil = ms + cfg::kPeekMs;
  if (b.down && !b.consumed && b.heldFor(ms) >= cfg::kHoldCancelMs) {
    b.consumed = true;
    cancelFrom = smooth.shown >= 0 ? smooth.shown : face::ticksLeft(clk.remaining(now) / 1e6, clk.duration_us / 1e6);
    cancelAt = ms;
    clk.cancel();
    enter(St::Cancelling);
    return;
  }
  if (b.released && !b.consumed) {
    if (st == St::Running) { clk.pause(now); pausedByFlat = false; enter(St::Paused); }
    else { clk.resume(now); pausedByFlat = false; enter(St::Running); }
  }
  if (c.down && !c.consumed && c.heldFor(ms) >= cfg::kHoldExtendMs) {
    c.consumed = true;
    clk.extend(cfg::kExtendUs);
    peekUntil = ms + cfg::kPeekMs;  // show the new remaining
    saveSession();
    Serial.println("EXTEND +5 min");
  }
  // lay flat to pause / stand up to resume (design.md §5.4)
  // Only real transitions count: Unknown -> Flat (boot, tilt just enabled)
  // must not pause a block restored after a reset.
  Tilt::Pose p = tilt.pose();
  if (p != lastPose && lastPose != Tilt::kUnknown) {
    if (p == Tilt::kFlat && st == St::Running) { clk.pause(now); pausedByFlat = true; enter(St::Paused); }
    else if (p == Tilt::kUpright && st == St::Paused && pausedByFlat) { clk.resume(now); pausedByFlat = false; enter(St::Running); }
  }
  if (st == St::Running) {
    int64_t rem = clk.remaining(now);
    if (rem <= 0) { enter(St::Done); return; }
    int t = face::ticksLeft(rem / 1e6, clk.duration_us / 1e6);
    if (t != lastSavedTicks) { lastSavedTicks = t; saveSession(); }  // per LED: cheap
  }
}

static void handleDone(uint32_t ms) {
  Key &a = input.a, &b = input.b, &c = input.c;
  if (b.down && !b.consumed && b.heldFor(ms) >= cfg::kHoldCancelMs) {
    b.consumed = true;
    startBlock(lastDurationUs);
    return;
  }
  if (a.pressed || c.pressed || (b.released && !b.consumed)) { enter(St::Set); return; }
  if (doneDown != 255 && tilt.downHeld((doneDown + 2) & 3, cfg::kFlipDwellMs, ms)) {
    Serial.println("FLIP -> repeat");
    startBlock(lastDurationUs);
    return;
  }
  if (ms - doneAt < cfg::kDoneFastMs && ms - lastChirp >= cfg::kChirpEveryMs) {
    lastChirp = ms;
    sound.chirps(3);
  }
}

static const int kSettingsItems = 5;
static void settingsValue(char* item, char* buf, size_t n) {
  switch (settingsItem) {
    case 0: *item = 'M'; snprintf(buf, n, "%d", settings.brightness); break;
    case 1: *item = 'B'; snprintf(buf, n, "%s", settings.bars == 0 ? "A" : settings.bars == 1 ? "1" : "0"); break;
    case 2: *item = 'I'; snprintf(buf, n, "%s", settings.barLevel == 0 ? "5" : settings.barLevel == 1 ? "10" : "25"); break;
    case 3: *item = 'S'; snprintf(buf, n, "%d", settings.sound); break;
    default: *item = 'O';
      if (settings.orientation == 0) snprintf(buf, n, "A"); else snprintf(buf, n, "%d", settings.orientation - 1);
      break;
  }
}

static void handleSettings() {
  int dir = input.c.pressed ? 1 : input.a.pressed ? -1 : 0;
  if (dir) {
    auto wrap = [](int v, int lo, int hi) { return v < lo ? hi : v > hi ? lo : v; };
    switch (settingsItem) {
      case 0: settings.brightness = wrap(settings.brightness + dir, 1, 8); break;
      case 1: settings.bars = wrap(settings.bars + dir, 0, 2); break;
      case 2: settings.barLevel = wrap(settings.barLevel + dir, 0, 2); break;
      case 3: settings.sound = !settings.sound; break;
      case 4: settings.orientation = wrap(settings.orientation + dir, 0, 4); break;
    }
    applySettings();
  }
  if (input.b.released && !input.b.consumed) {
    if (++settingsItem >= kSettingsItems) {
      persist::saveSettings(settings);
      enter(St::Set);
    }
  }
}

// ---------------------------------------------------------------- output
// Done: matrix and bars take turns. 2 Hz (250 ms each) for the first minute,
// then 0.5 Hz (1 s each). True = the matrix's turn.
static bool doneMatrixPhase(uint32_t ms) {
  uint32_t e = ms - doneAt;
  return e < cfg::kDoneFastMs ? (e % 500) < 250 : (e % 2000) < 1000;
}

static Frame currentFrame(uint32_t ms, int64_t now, int* duty) {
  int level = settings.brightness;
  *duty = level;
  double total = clk.duration_us / 1e6, rem = clk.remaining(now) / 1e6;
  switch (st) {
    case St::Set: return face::durationFrame(minutes);
    case St::Running:
      if ((int32_t)(peekUntil - ms) > 0) return face::peekFrame(rem);
      return smooth.frame(rem, total, clk.elapsed(now) / 1e6, false);
    case St::Paused:
      // Paused: static at the lowest duty, 1/16 (Steel preferred this to
      // breathing, 21 Nov). Peek shows at the normal level.
      if ((int32_t)(peekUntil - ms) > 0) return face::peekFrame(rem);
      *duty = 1;
      return smooth.frame(rem, total, clk.elapsed(now) / 1e6, true);
    case St::Done: {
      // Matrix at 1/16, alternating with the bars (Steel, 21 Nov).
      *duty = 1;
      Frame f;
      f.fill(doneMatrixPhase(ms));
      return f;
    }
    case St::Settings: {
      char item, val[4];
      settingsValue(&item, val, sizeof val);
      return face::settingsFrame(item, val);
    }
    case St::Cancelling: {
      uint32_t e = ms - cancelAt;
      if (e >= cfg::kCancelWipeMs) { enter(St::Set); return face::durationFrame(minutes); }
      return face::litTail(cancelFrom - (int)(cancelFrom * e / cfg::kCancelWipeMs));
    }
  }
  return Frame{};
}

static void renderBars(uint32_t ms, int64_t now) {
  if (!power.boost() || ms - lastBarsMs < 50) return;
  float dt = (ms - lastBarsMs) / 1000.0f;
  lastBarsMs = ms;
  float tau = 0.6f;
  bars.clear();
  switch (st) {
    case St::Set: {
      int lv = power.level();
      if (lv >= 0) {
        float f = lv / 100.0f;
        if (power.charging()) f = fminf(f, (ms % 3000) / 2500.0f);  // animate upward
        Rgb g = colors::kGreen;
        g.r *= 0.35f; g.g *= 0.35f; g.b *= 0.35f;
        bars.gauge(f, g);
      }
      break;
    }
    case St::Running: {
      float f = clk.remaining(now) / (float)clk.duration_us;
      Rgb col = f <= 0.05f ? colors::kRed : f <= 0.20f ? colors::kAmber : colors::kTeal;
      bars.gauge(f, col);
      // A fresh block starts full (all 5); the bleed-compensated gauge drops the
      // top LED, so hold it for a moment and let the cross-fade take it out.
      if (clk.elapsed(now) < (int64_t)(cfg::kBarTopHoldS * 1e6)) bars.setPos(4, col, 1.0f);
      break;
    }
    case St::Paused: bars.setPos(0, colors::kAmber, 0.4f); break;
    case St::Done:
      if (!doneMatrixPhase(ms)) for (int i = 0; i < 5; ++i) bars.setPos(i, colors::kRed, 1.0f);
      tau = 0;  // alternate crisply with the matrix flash
      break;
    default: break;
  }
  if (power.low() && (ms % 5000) < 200) bars.setPos(0, colors::kRed, 1.0f, 2);
  bars.update(dt, tau);
}

// ---------------------------------------------------------------- console
static void dumpFrame() {
  const Frame& f = matrix.shown();
  Serial.printf("FRAME %s up=%u duty=%d on=%d\n", stName(st), matrix.up(), matrix.duty(), matrixOn);
  for (int r = 0; r < cfg::kH; ++r) {
    char line[cfg::kW + 1];
    for (int c = 0; c < cfg::kW; ++c) line[c] = f.get(r, c) ? '#' : '.';
    line[cfg::kW] = 0;
    Serial.printf("  %s\n", line);
  }
}

static void status() {
  int64_t now = nowUs();
  Serial.printf("STATUS st=%s minutes=%d dur=%.1fs rem=%.1fs ticks=%d paused_by_flat=%d warp=%llds\n", stName(st),
                minutes, clk.duration_us / 1e6, clk.remaining(now) / 1e6,
                face::ticksLeft(clk.remaining(now) / 1e6, clk.duration_us / 1e6), pausedByFlat,
                (long long)(warpUs / 1000000));
  Serial.printf("STATUS usb=%d boost=%d batt=%d%% vbat=%umV charging=%d tilt=%d/%d pose=%d down=%u up=%u bars_ok=%d\n",
                power.onUsb(), power.boost(), power.level(), power.vbat(), power.charging(), tilt.available(),
                tilt.usable(), (int)tilt.pose(), tilt.downEdge(), matrix.up(), bars.ok());
  Serial.printf("STATUS settings bri=%d bars=%d barlvl=%d sound=%d orient=%d last_min=%d\n", settings.brightness,
                settings.bars, settings.barLevel, settings.sound, settings.orientation, settings.minutes);
}

static void console(uint32_t ms) {
  static char line[24];
  static int n = 0;
  while (Serial.available()) {
    char ch = Serial.read();
    if (ch != '\n' && ch != '\r') { if (n < (int)sizeof line - 1) line[n++] = ch; continue; }
    line[n] = 0;
    n = 0;
    switch (line[0]) {
      case 'a': input.simulate(0, 120, ms); break;
      case 'b': input.simulate(1, 120, ms); break;
      case 'c': input.simulate(2, 120, ms); break;
      case 'A': input.simulate(0, 1000, ms); break;
      case 'B': input.simulate(1, cfg::kHoldCancelMs + 200, ms); break;
      case 'C': input.simulate(2, cfg::kHoldExtendMs + 200, ms); break;
      case 'X': input.simulate(0, cfg::kChordSettingsMs + 300, ms); input.simulate(2, cfg::kChordSettingsMs + 300, ms); break;
      case 'p': input.simulatePower(); break;
      case 'f': dumpFrame(); break;
      case 's': status(); break;
      case 'w': warpUs += (int64_t)atol(line + 1) * 1000000; Serial.printf("WARP +%lds\n", atol(line + 1)); break;
      case 't': Serial.printf("SOUND test chirps (enabled=%d)\n", sound.enabled()); sound.chirps(3); break;
      case 'r': Serial.printf("TILT raw x=%+.3f y=%+.3f z=%+.3f pose=%d down=%u\n", tilt.raw()[0], tilt.raw()[1],
                              tilt.raw()[2], (int)tilt.pose(), tilt.downEdge()); break;
      case '?': Serial.println("keys: a b c (click) A B C (hold) X (A+C settings) p (power) f (frame) s (status) "
                               "wN (warp N s) r (tilt raw) t (test chirps)"); break;
      default: break;
    }
  }
}

// ---------------------------------------------------------------- main
void setup() {
  auto mc = M5.config();
  mc.serial_baudrate = 115200;
  mc.internal_imu = false;  // our own 0x68 driver (tilt.h)
  mc.internal_spk = false;
  mc.internal_mic = false;
  M5.begin(mc);

  persist::loadSettings(&settings);
  minutes = settings.minutes;

  // BMI270 must be initialised with the boost OFF (the MPU6886 shares 0x68).
  power.setBoost(false);
  delay(300);
  bool imu = tilt.begin();

  power.begin();
  matrix.begin();
  bars.begin();
  sound.begin();
  applySettings();
  boostPolicy();

  persist::Record r;
  int64_t now = nowUs();
  if (persist::load(&r) && r.state != persist::kNone) {
    int64_t dur = (int64_t)r.duration_s * 1000000, rem = (int64_t)r.remaining_ms * 1000;
    lastDurationUs = dur;
    if (r.state == persist::kDone) {
      st = St::Done;
      doneAt = lastChirp = millis();
    } else {
      clk.restore(now, dur, rem, r.state == persist::kPaused);
      pausedByFlat = r.flags & 1;
      st = r.state == persist::kPaused ? St::Paused : St::Running;
    }
  }
  Serial.printf("\nMatrix-Timer v1 boot: state=%s imu=%d bars=%d usb=%d reset=%d\n", stName(st), imu, bars.ok(),
                power.onUsb(), (int)esp_reset_reason());
}

void loop() {
  uint32_t ms = millis();
  int64_t now = nowUs();

  input.update(ms);
  power.poll(ms);
  boostPolicy();
  tilt.update(ms);
  console(ms);
  orientationPolicy();

  if (input.pwrClicked) { matrixOn = !matrixOn; Serial.printf("MATRIX %s\n", matrixOn ? "on" : "off"); }

  switch (st) {
    case St::Set: handleSet(ms); break;
    case St::Running:
    case St::Paused: handleRunPause(ms, now); break;
    case St::Done: handleDone(ms); break;
    case St::Settings: handleSettings(); break;
    case St::Cancelling: break;
  }
  lastPose = tilt.pose();

  int duty;
  Frame f = currentFrame(ms, now, &duty);
  if (!power.onUsb() && duty > cfg::kBatteryDuty) duty = cfg::kBatteryDuty;  // 2/16 on battery (Steel, 21 Nov)
  matrix.applyDuty(matrixOn ? duty : 0);
  matrix.show(f);
  renderBars(ms, now);
  sound.update(ms);

  if (st == St::Set && !power.onUsb() && ms - input.lastActivity() > cfg::kIdleOffMs && ms > cfg::kIdleOffMs) {
    Serial.println("IDLE power off");
    Serial.flush();
    power.powerOff();
  }
  delay(5);
}
