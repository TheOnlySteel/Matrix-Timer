// Bars: the Bottom2's 10 SK6812s as two mirrored 5-LED gauges.
//
// Driver (hardware.md §6, bench check 4): SPI2 as a waveform generator on G5,
// 3.2 MHz, 4 SPI bits per LED bit (0 -> 1000, 1 -> 1100), GRB, with >= 80 us
// low around each frame. IDF spi_master with SCLK unassigned (Arduino's
// SPIClass refuses sck = -1 on the C61).
//
// Chain in panel coordinates (rotation 0), as confirmed on the desk with the
// gauge running (21 Nov): x counts from the USB-C end,
//   bar A along the panel top (button) edge: chain 4 - x
//   bar B along the panel bottom edge:       chain 5 + x
// (Bench check 4 read the chain the other way round, 0 at the USB end; the
// running gauge showed it filling from the top, so the desk check wins.)
// Logical position 0 is the bar's "start": the bottom when the bars stand
// vertical (USB edge up or down), the left when they lie horizontal.
#pragma once
#include <driver/spi_master.h>
#include <math.h>

#include "config.h"

struct Rgb { float r = 0, g = 0, b = 0; };

namespace colors {
constexpr Rgb kTeal{0, 190, 170}, kAmber{255, 140, 0}, kRed{255, 30, 10}, kGreen{40, 220, 60};
}

class Bars {
 public:
  void begin() {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = cfg::kBarPin;
    bus.miso_io_num = -1;
    bus.sclk_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = sizeof buf_;
    ok_ = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) == ESP_OK;
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz = 3200000;
    dev.mode = 0;
    dev.spics_io_num = -1;
    dev.queue_size = 1;
    ok_ = ok_ && spi_bus_add_device(SPI2_HOST, &dev, &dev_) == ESP_OK;
    dma_ = (uint8_t*)heap_caps_malloc(sizeof buf_, MALLOC_CAP_DMA);
    ok_ = ok_ && dma_;
  }
  bool ok() const { return ok_; }

  // Brightness cap as a fraction of full scale (5 / 10 / 25 %).
  void setCap(float cap) { cap_ = cap; }
  void setUp(uint8_t up) { up_ = up & 3; }

  // Targets, logical: pos 0..4 on both bars (mirrored), linear 0..1 per channel
  // already multiplied by the gauge level. `left/right` lets the low-battery
  // blink address one bar only.
  void clear() { for (auto& t : target_) t = Rgb{}; }
  void setPos(int pos, Rgb c, float level, int which = 3) {
    int x = (up_ == 0 || up_ == 1) ? pos : 4 - pos;  // x from the USB-C end
    Rgb v{c.r * level, c.g * level, c.b * level};
    if (which & 1) target_[4 - x] = v;      // bar A
    if (which & 2) target_[5 + x] = v;      // bar B
  }

  // 5-LED analogue gauge: the topmost lit LED carries the remainder as
  // brightness; the bottom LED never fades below 15 % while any remains.
  void gauge(float fraction, Rgb c) {
    float x = fraction < 0 ? 0 : (fraction > 1 ? 1 : fraction);
    x *= 5;
    for (int i = 0; i < 5; ++i) {
      float lv = x - i;
      lv = lv < 0 ? 0 : (lv > 1 ? 1 : lv);
      if (i == 0 && x > 0 && x < 0.15f) lv = 0.15f;
      setPos(i, c, lv);
    }
  }

  // Ease current toward target (time constant tau_s; 0 = snap), then send.
  void update(float dt_s, float tau_s) {
    float a = tau_s <= 0 ? 1.0f : 1.0f - expf(-dt_s / tau_s);
    for (int i = 0; i < cfg::kBarLeds; ++i) {
      cur_[i].r += (target_[i].r - cur_[i].r) * a;
      cur_[i].g += (target_[i].g - cur_[i].g) * a;
      cur_[i].b += (target_[i].b - cur_[i].b) * a;
    }
    send();
  }

  // Force black on the wire right now (before the 5 V rail goes off).
  void blackout() {
    clear();
    for (auto& c : cur_) c = Rgb{};
    send();
  }

 private:
  // gamma 2.2 on the 0..255 value, then the cap; anything non-zero stays >= 1
  uint8_t out(float v) const {
    if (v <= 0.5f) return 0;
    float g = powf(v / 255.0f, 2.2f) * 255.0f * cap_;
    int o = (int)lroundf(g);
    if (o < 1) o = 1;
    if (o > cfg::kBarHardCap) o = cfg::kBarHardCap;
    return (uint8_t)o;
  }

  void send() {
    if (!ok_) return;
    uint8_t* p = buf_;
    for (int i = 0; i < kLatch; ++i) *p++ = 0;
    for (int i = 0; i < cfg::kBarLeds; ++i) {
      const uint8_t grb[3] = {out(cur_[i].g), out(cur_[i].r), out(cur_[i].b)};
      for (uint8_t v : grb)
        for (int b = 7; b >= 0; b -= 2) {
          uint8_t hi = (v >> b) & 1 ? 0xC : 0x8;
          uint8_t lo = (v >> (b - 1)) & 1 ? 0xC : 0x8;
          *p++ = (hi << 4) | lo;
        }
    }
    for (int i = 0; i < kLatch; ++i) *p++ = 0;
    memcpy(dma_, buf_, sizeof buf_);
    spi_transaction_t t = {};
    t.length = sizeof buf_ * 8;
    t.tx_buffer = dma_;
    spi_device_polling_transmit(dev_, &t);
  }

  static constexpr int kLatch = 40;  // 40 bytes at 3.2 MHz = 100 us low
  uint8_t buf_[kLatch * 2 + cfg::kBarLeds * 12];
  uint8_t* dma_ = nullptr;
  spi_device_handle_t dev_ = nullptr;
  bool ok_ = false;
  float cap_ = 0.10f;
  uint8_t up_ = 0;
  Rgb target_[cfg::kBarLeds], cur_[cfg::kBarLeds];
};
