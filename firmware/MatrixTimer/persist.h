// Persist: the running session in PM1 RTC RAM (survives ESP resets and ESP
// power-off, not PM1 power loss; hardware.md §2.4), settings in NVS.
#pragma once
#include <M5Unified.h>
#include <Preferences.h>

#include "config.h"

namespace persist {

enum State : uint8_t { kNone = 0, kRunning = 1, kPaused = 2, kDone = 3 };

struct __attribute__((packed)) Record {
  uint8_t magic[2];      // 'M', 'T'
  uint8_t version;       // 1
  uint8_t state;         // State
  uint8_t flags;         // bit0: paused by laying flat
  uint8_t reserved[2];
  uint32_t duration_s;
  uint32_t remaining_ms;
  uint8_t crc;           // CRC8 of the 15 bytes before it
};
static_assert(sizeof(Record) == 16, "record must be 16 bytes");

constexpr uint8_t kPm1 = 0x6E, kRtcRam = 0xA0;

inline uint8_t crc8(const uint8_t* p, size_t n) {
  uint8_t c = 0;
  while (n--) {
    c ^= *p++;
    for (int i = 0; i < 8; ++i) c = c & 0x80 ? (c << 1) ^ 0x07 : c << 1;
  }
  return c;
}

// The 16-byte record lives at RTC RAM 0xA0..0xAF.
inline bool save(State st, uint8_t flags, uint32_t duration_s, uint32_t remaining_ms) {
  Record r{};
  r.magic[0] = 'M'; r.magic[1] = 'T'; r.version = 1;
  r.state = st; r.flags = flags; r.duration_s = duration_s; r.remaining_ms = remaining_ms;
  r.crc = crc8((const uint8_t*)&r, 15);
  return M5.In_I2C.writeRegister(kPm1, kRtcRam, (const uint8_t*)&r, 16, 100000);
}

inline bool load(Record* out) {
  uint8_t buf[16];
  if (!M5.In_I2C.readRegister(kPm1, kRtcRam, buf, sizeof buf, 100000)) return false;
  if (crc8(buf, 15) != buf[15]) return false;
  memcpy(out, buf, 16);
  return out->magic[0] == 'M' && out->magic[1] == 'T' && out->version == 1 && out->state <= kDone &&
         out->duration_s > 0;
}

struct Settings {
  uint8_t brightness = 4;   // matrix duty 1..8 (/16); 4/16 on USB per Steel's desk check
  uint8_t bars = 0;         // 0 Auto, 1 On, 2 Off
  uint8_t barLevel = 1;     // 0: 5 %, 1: 10 %, 2: 25 %
  uint8_t sound = 1;        // 1 chirp, 0 silent
  uint8_t orientation = 0;  // 0 Auto, 1..4 = fixed up edge 0..3
  uint16_t minutes = cfg::kDefaultMinutes;
};

inline void loadSettings(Settings* s) {
  Preferences p;
  p.begin("mt", true);
  s->brightness = p.getUChar("bri", s->brightness);
  s->bars = p.getUChar("bars", s->bars);
  s->barLevel = p.getUChar("blvl", s->barLevel);
  s->sound = p.getUChar("snd", s->sound);
  s->orientation = p.getUChar("ori", s->orientation);
  s->minutes = p.getUShort("min", s->minutes);
  p.end();
  if (s->brightness < 1 || s->brightness > 8) s->brightness = 4;
  if (s->bars > 2) s->bars = 0;
  if (s->barLevel > 2) s->barLevel = 1;
  if (s->sound > 1) s->sound = 1;
  if (s->orientation > 4) s->orientation = 0;
  if (s->minutes < cfg::kMinMinutes || s->minutes > cfg::kMaxMinutes) s->minutes = cfg::kDefaultMinutes;
}

inline void saveSettings(const Settings& s) {
  Preferences p;
  p.begin("mt", false);
  p.putUChar("bri", s.brightness);
  p.putUChar("bars", s.bars);
  p.putUChar("blvl", s.barLevel);
  p.putUChar("snd", s.sound);
  p.putUChar("ori", s.orientation);
  p.putUShort("min", s.minutes);
  p.end();
}

}  // namespace persist
