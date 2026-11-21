// Matrix-Timer firmware: constants. See docs/design.md and docs/hardware.md.
#pragma once
#include <stdint.h>

namespace cfg {

// --- grid (design.md §3.1) ---
constexpr int kW = 16, kH = 16, kN = kW * kH;   // 256 ticks per block
// Active LED blink, tuned on the desk with Steel (21 Nov): 1 s on, 0.5 s off,
// in a rhythm that never breaks. The display advances to the next LED only at
// the start of a blink cycle (face.h SmoothGrid), and in the off phase just
// before a hand-over the outgoing and the incoming LED go dark together, so
// the new active LED blinks back on, on schedule. An LED can lag the true
// time by up to one cycle; Done still uses the exact time.
constexpr double kBlinkPeriodS = 1.5;
constexpr double kBlinkOnS = 1.0;
constexpr double kMinSliceForBlinkS = 1.5;      // below one blink cycle per LED: steady

// --- durations (design.md §5.2) ---
constexpr int kMinMinutes = 1, kMaxMinutes = 240, kDefaultMinutes = 25;
constexpr int64_t kExtendUs = 5LL * 60 * 1000000;   // C hold: +5 min

// --- buttons (design.md §5.2) ---
constexpr uint32_t kLockoutMs = 50;        // our own per-button lockout on top of M5Unified's
constexpr uint32_t kHoldCancelMs = 1500;   // B hold: cancel / repeat
constexpr uint32_t kHoldExtendMs = 1000;   // C hold while running: +5 min
constexpr uint32_t kChordSettingsMs = 2000;// A+C hold in Set: settings
constexpr uint32_t kRepeatDelayMs = 400;   // A/C hold in Set: start repeating
constexpr uint32_t kRepeatFastAfterMs = 1400;
constexpr uint32_t kRepeatSlowMs = 250, kRepeatFastMs = 100;  // 4 Hz, then 10 Hz
constexpr uint32_t kPeekMs = 2000;
constexpr uint32_t kCancelWipeMs = 300;

// --- bars (design.md §4, hardware.md §5.3/§6) ---
constexpr int kBarPin = 5;          // G5, SK6812 data
constexpr int kBarLeds = 10;
constexpr uint8_t kBarHardCap = 64; // 25 % of 255: never exceeded, whatever the setting

// --- done (design.md §5.5) ---
constexpr uint32_t kDoneFastMs = 60000;   // 2 Hz flash + chirps for 1 minute, then 0.5 Hz
constexpr uint32_t kChirpEveryMs = 10000;
constexpr uint16_t kBuzzerHz = 2700;

// --- power (design.md §7, hardware.md §2.4) ---
constexpr uint16_t kLvpMv = 3300;           // PM1 low-voltage cut-off
constexpr uint32_t kIdleOffMs = 10UL * 60 * 1000;
constexpr uint16_t kUsbMv = 4500;           // VIN above this = on USB
constexpr int kLowBatteryPct = 15;
constexpr int kBatteryDuty = 2;             // matrix duty cap on battery: 2/16

// --- tilt (design.md §5.4) ---
constexpr float kFlatG = 0.8f;
constexpr uint32_t kFlatDwellMs = 1000, kUprightDwellMs = 500, kRotateDwellMs = 1000;
constexpr uint32_t kFlipDwellMs = 800;

}  // namespace cfg
