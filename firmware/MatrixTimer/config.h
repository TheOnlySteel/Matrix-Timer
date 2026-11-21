// Matrix-Timer firmware: constants. See docs/design.md and docs/hardware.md.
#pragma once
#include <stdint.h>

namespace cfg {

// --- grid (design.md §3.1) ---
constexpr int kW = 16, kH = 16, kN = kW * kH;   // 256 ticks per block
// Active LED blink (Steel, 21 Nov: at least 1 s on and 1 s off; 3 s felt a
// little slow). One 2.5 s cycle; the on-time shrinks from 1.5 s to 1 s as the
// LED's slice is used up, so it still drains visibly but never flickers.
constexpr double kBlinkPeriodS = 2.5;
constexpr double kBlinkMinOnS = 1.0, kBlinkMaxOnS = 1.5;
constexpr double kMinSliceForBlinkS = 2.5;      // below one blink cycle per LED: steady

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
