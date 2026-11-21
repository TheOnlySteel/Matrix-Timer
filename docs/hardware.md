# Hardware deep-dive: Core Matrix + M5GO Bottom2

Research date: 21 November 2026. The Core Matrix launched on 6 November 2026, so the documentation and libraries are two weeks old and moving. Every claim below is tagged with its source; **[V]** means verified against vendor docs *and* library source, **[D]** vendor docs only, **[S]** library source only, **[I]** my inference, **[?]** unresolved and must be checked on the bench (see [§9](#9-bench-checks-before-writing-app-code)).

## 0. Product identities

| What you have | Exact product | SKU | Confidence |
|---|---|---|---|
| "Core Matrix" | **M5Stack CoreMatrix** (also styled "Core Matrix", sold as "Core Matrix Dev Kit"), ESP32-C61 controller with a 16×16 LED matrix | C158 | High. Not to be confused with the ATOM Matrix (ESP32, 5×5 RGB). |
| "M5GO Battery Bottom2" | **Base M5GO Bottom2**, an expansion base designed for the **Core2**. Two hardware revisions exist. | A014-C (MPU6886 IMU) or A014-C-V13 (BMI270 IMU) | High on the product; **which revision you have is [?]** and matters (§5.4). |

The Bottom2 is not listed as compatible with the Core Matrix anywhere. It works because both follow the 30-pin M-Bus convention, but every pin it uses lands on a different Core Matrix GPIO than on a Core2. Section 5 is the mapping.

Sources: [CoreMatrix docs](https://docs.m5stack.com/en/core/CoreMatrix), [Bottom2 v1.3 docs](https://docs.m5stack.com/en/base/Base_M5GO_Bottom2_v1.3), [Bottom2 docs](https://docs.m5stack.com/en/base/Base_M5GO_Bottom2), M5Unified @ `96576a1` (6 Nov 2026), M5GFX `board_spec/boards/m5stack_corematrix.json`, [M5PM1 datasheet v1.9](https://github.com/m5stack/M5PM1/tree/main/docs), ESP-IDF `soc/esp32c61/include/soc/soc_caps.h`, schematics `CoreMatrix_V0.7_SCH_PDF_20260609.pdf`, `CoreMatrix_SCH_LED.pdf`, `SCH_M5GO-Bottom2_SCH_Main_V1.31`, `A014-C_Sch_M5GO2.pdf`.

## 1. The three facts that shape the design

1. **The matrix is monochrome and 1-bit.** 256 white LEDs behind a Titan Micro **TM1680** driver on I2C. Each pixel is on or off; the only dimming is a *global* 16-step PWM duty. No per-pixel brightness, no colour. **[V]**
2. **The ESP32-C61 has no RMT peripheral**, the block every common NeoPixel library uses. M5Unified explicitly disables its LED-strip driver on this chip. Driving the Bottom2's SK6812 bars needs our own driver (SPI-encoded waveform, §6). **[V]**
3. **The buttons are not on ESP GPIOs.** KEY1–3 hang off the **M5PM1** power-management MCU and are read by I2C polling (or its IRQ line). Everything human-facing (buttons, matrix, IMU, power) shares one I2C bus on G0/G1. **[V]**

## 2. Core Matrix at a glance

| Block | Part / value | Interface | Source |
|---|---|---|---|
| SoC | ESP32-C61HR8, single-core RISC-V @160 MHz, Wi-Fi 6 (2.4 GHz); BLE 5 in silicon (board docs only advertise Wi-Fi) | | [V] |
| Memory | 16 MB flash, 8 MB quad PSRAM | | [D] |
| Matrix | 16×16 white LEDs, TM1680 (LQFP52), 24ROW×16COM NMOS mode | I2C 0x72 (0xE4 8-bit) | [V] |
| IMU | Bosch BMI270, 6-axis | I2C 0x68; INT1 → PM1 GPIO4 | [V] |
| Buttons | KEY1/2/3 = BtnA/B/C, plus power button | PM1 GPIO0/1/2; PWR_BTN on PM1 | [V] |
| Buzzer | MLT-8530 passive | IOE1 IO2 enable, IO11 PWM | [D] |
| PMIC | **M5PM1** (sold separately as "Stamp Timer Power2"), custom-firmware MCU | I2C 0x6E, IRQ → ESP **G2** | [V] |
| IO expander | **M5IOE1** | I2C 0x4F | [V] |
| Charger | LGS4056, 180 mA (default) or 650 mA | CHG_PROG on IOE1 IO3, CHG_STAT on IOE1 IO8 (low = charging) | [D] + schematic text |
| Power path | AW32901 USB OVP → TPS2116 USB/VBAT mux; SY7088 5 V boost | | schematic text |
| Storage | microSD (SPI: SCK G25, MISO G26, MOSI G27, CS G28) | power IOE1 IO1, detect IOE1 IO10 | [D] |
| USB | Type-C, native USB on G12 (D−) / G13 (D+): USB-Serial-JTAG, no bridge chip | | [D] pins + [I] peripheral (C61 has `SOC_USB_SERIAL_JTAG_SUPPORTED`, no OTG) |
| Battery | **None included.** MX1.25-2P connector, plus M-Bus pin 30 BAT | | [D] |
| Size | 54 × 54 × 11.6 mm, 28.9 g | | [D] |

### 2.1 I2C bus (the only one: `SOC_I2C_NUM = 1`)

SDA **G0**, SCL **G1**. M5Unified runs it at 100 kHz for the PM1/IOE1 and M5GFX at 400 kHz for the TM1680; both libraries share the port.

| Addr | Device | Notes |
|---|---|---|
| 0x4F | M5IOE1 | Rail switches, charger control, buzzer PWM |
| 0x68 | BMI270 (on Core Matrix) | **Collision risk with the Bottom2's IMU, §5.4** |
| 0x6E | M5PM1 | Device ID 0x2050; buttons, power, ADC, timer, 32-byte RTC RAM |
| 0x72 | TM1680 | Write-only: it does not ACK reads, so the usual probe-by-read fails |

The TM1680 sits behind 2N7002DW level shifters; the Grove port (PORT.A: G0/G1, 5 V) and the M-Bus I2C pins are separate level-shifted copies of the same bus (MTXS0102). Anything on Grove or the Bottom2 shares this bus.

### 2.2 The LED matrix in detail

- **Data path.** M5GFX keeps a 1-bit frame buffer and pushes the **whole frame, 32 bytes plus an address byte**, on every `display()`. At 400 kHz that is roughly 0.8 ms of bus time. **[S]**
- **Init sequence** (`Panel_TM1680`): `SYS_DIS 0x80`, `COM_16NMOS 0xA4`, `RC_MASTER 0x98`, `SYS_EN 0x81`, `PWM_DUTY 0xB0+1` (2/16 duty), `LED_ON 0x83`. **[S]**
- **Brightness.** `setBrightness(b)` sends `0xB0 + ((b*16)>>8)`, so 0–255 collapses to **16 duty steps (1/16…16/16)**; `0` sends `LED_OFF`. The vendor example uses 128 (9/16). **[S]**
- **Auto-push.** `Panel_TM1680` sets `_auto_display = true`: each draw call pushes the full frame. Wrap a frame's drawing in `startWrite()`/`endWrite()` or draw into a 1-bit `M5Canvas` and push once, or every `drawPixel` costs 0.8 ms. **[S]**
- **No invert command.** Inversion is done in software during packing. **[S]**
- **Power.** LED rail `LEDS_3V3_L3B` through an MT9700 load switch (≈1 A limit) enabled by **IOE1 IO4** (`PYB_LEDS_EN`); M5GFX/M5Unified enable it at boot. **[D]** + schematic text
- **Sleep.** `setSleep(true)` sends `LED_OFF` + `SYS_DIS`; the TM1680 keeps its RAM, so the image survives a sleep/wake. **[S]**
- **Greyscale is not available.** Software frame-flipping could fake 2–4 levels, but it beats against the TM1680's internal scan and steals bus time from button polling. The design does not use it. **[I]**
- **Rotation.** `M5.Display.setRotation(0..3)` works as on any M5GFX panel; the vendor example uses 1. Which way is "up" relative to the buttons is **[?]**.

### 2.3 Buttons

| Button | Electrical | Read by | Library |
|---|---|---|---|
| KEY1 / BtnA | PM1 GPIO0, pull-up on 3V3_L1, active low | I2C poll of PM1 GPIO input register in `M5.update()` | `M5.BtnA` |
| KEY2 / BtnB | PM1 GPIO1 | same | `M5.BtnB` |
| KEY3 / BtnC | PM1 GPIO2 | same | `M5.BtnC` |
| Power | PM1 `PWR_BTN` (internal pull-up) | PM1 key-press register | `M5.BtnPWR` (clicked / hold) |

M5Unified skips the button update on an I2C error rather than reporting a phantom press. **[S]** Because the PM1 has an **IRQ output wired to ESP G2** (PM1 GPIO3, configured by M5Unified as push-pull IRQ), button edges also raise IRQ status bits, so firmware can wake from light sleep on G2 instead of polling. **[S]** + datasheet

**Power-button behaviour is set by PM1 firmware, not ours** (datasheet §V.3): single click = **reset**, double click = **power off**, long press = **download mode**; from off, a click powers on. Timing is configurable in `BTN_CFG_1` (single 125 ms–1 s, double 125 ms–1 s, long 1–4 s). Single-click reset can be disabled (`SINGLE_RESET_DIS`, M5PM1 lib `setSingleResetDisable(true)`), after which a single click raises an interrupt instead. Download mode can be locked out (`DL_LOCK`). **[V]**

### 2.4 Power management (M5PM1 + M5IOE1)

Rails, from the vendor power page and M5Unified:

| Rail | Source | Feeds | Control |
|---|---|---|---|
| 3V3_L3A | PM1 DC/DC | ESP32-C61, IOE1, I2C pull-ups | PM1 (`setDcdcEnable`); off = system off |
| 3V3_L1 | PM1 LDO | BMI270, key pull-ups | PM1 (`setLdoEnable`) |
| 3V3_L3B (LEDS) | load switch | TM1680 LED supply | IOE1 IO4 |
| L0: 5V_INOUT + M5BUS_3V3_L0 | SY7088 boost from the system rail | **M-Bus 5 V (pin 28) and M-Bus 3V3 (pin 12)**, i.e. everything on the Bottom2 | PM1 boost enable; M5Unified turns it **on** in `Power.begin()` (`M5pm1.setExtOutput(true)`) |
| Grove 5 V/3V3 | gated from L0 | PORT.A | IOE1 IO5; this is what `M5.Power.setExtOutput()` toggles on CoreMatrix |
| TF / buzzer | | | IOE1 IO1 / IO2 |

Useful PM1 features (datasheet v1.9):

- **Low-voltage cut-off** `BATT_LVP` (0x08): threshold = 2.0 V + reg × 7.81 mV; default 0x40 → **2.50 V**, too low for a single Li-ion cell's health. Set ≈3.3 V (`setBatteryLvp(3300)`). On battery only, the PM1 powers the system off below it.
- **32-byte RTC RAM** (0xA0–0xBF): survives ESP resets and ESP power-off, *not* PM1 power loss. This is where an in-progress timer should persist.
- **Seconds timer** (0x38–0x3D): counts down up to ~2³¹ s and can then set a flag, restart, **power on**, or power off the system.
- **Power events** (battery add/remove, 5VIN add/remove) raise IRQ status bits.
- **I2C idle sleep:** if `SLEEP[3:0]` is non-zero the PM1 sleeps when the bus is idle and needs up to 300 ms and an address match to wake. M5Unified retries transient NACKs. Leave it disabled (0) while the timer runs, since we poll buttons.
- Don't feed 5 V *into* M-Bus pin 28 while the boost is enabled. **[D]**

**Library coverage.** M5Unified's built-in `M5.Power.M5pm1` (public member) exposes boost on/off, GPIO, key state, IRQ and wake-source handling, but **not** `BATT_LVP`, single-click-reset disable, RTC RAM or the seconds timer. For those, use the standalone [M5PM1 library](https://github.com/m5stack/M5PM1) on M5Unified's bus (`pm1.begin(&M5.In_I2C, 0x6E, M5PM1_I2C_FREQ_100K)`, then `setBatteryLvp()`, `setSingleResetDisable()`, `writeRtcRAM()/readRtcRAM()`, `timerSet()`), or write the registers directly with `M5.In_I2C.writeRegister8(0x6E, reg, value, 100000)`. **[S]**

Measured by the vendor: **8.21 µA** shut down, **128 mA** "full load", both at 4.2 V. What "full load" includes is not stated. **[D]**

## 3. ESP32-C61 peripheral constraints that matter here

From `soc_caps.h` (ESP-IDF master), verified: **[V]**

| Capability | C61 | Consequence |
|---|---|---|
| RMT | **absent** | No Adafruit_NeoPixel / FastLED RMT path / M5Unified LED strip. Drive SK6812 via SPI (or I2S, or a cycle-counted bit-bang). |
| General-purpose SPI | **one** (SPI2; SPI1 is flash) | The microSD slot uses SPI2. Using SPI for the LED bars means **no microSD**, which the timer does not need. |
| I2C | one | Everything shares G0/G1 (§2.1). |
| LEDC | 6 channels | Unused (the buzzer PWM lives on the IOE1, not the ESP). |
| UART | 3 HP | Bottom2 PORT.C lands on G7/G8 (UART2 pins in the vendor map). |
| GPIO | 0–29 | |
| Sleep | light + deep; EXT1 wake per-pin level; RTC-IO on G0–G6 | PM1 IRQ on G2 can wake the chip. |
| BLE 5 / Wi-Fi 6 | yes | Not needed for v1. |

**Timekeeping.** Read elapsed time from `esp_timer_get_time()` (µs, monotonic, kept across light sleep using the RTC slow clock). No 32 kHz crystal is identified in the schematic text, so across light sleep the timebase relies on the calibrated internal RC; expect drift on the order of 0.1–1 %. **[I]** On USB power, skip light sleep and accuracy is crystal-grade.

## 4. M5GO Bottom2 at a glance

| Block | A014-C | A014-C-V13 (v1.3) | Source |
|---|---|---|---|
| LED bars | 10 × SK6812, two sides × 5, one chain | same | [D] |
| LED supply | +5 V (M-Bus pin 28), no level shifter on data | same | schematic text |
| IMU | MPU6886 | **BMI270** ("SDO=0 → 0x68 (default), SDO=1 → 0x69"; actual strap not legible) | [D] + [?] |
| Mic (out of scope) | LMD4737 (earlier SPM1423) PDM | LMD4020 PDM | [D] |
| Battery | 500 mAh @ 3.7 V Li-ion | same | [D] |
| Charger | TP4057 fed from the **pogo-pin dock** contacts | same | [D]; VIN source [I] |
| Ports | PORT.B, PORT.C (HY2.0-4P) | same | [D] |
| Protection / switch | none identified (no cut-off switch) | same | schematic text |
| Size | 54 × 54 × 15 mm | 54 × 54 × 15 mm, 30.9 g | [D] |

## 5. How the two connect (M-Bus mapping)

### 5.1 Pin-by-pin

Bottom2 net names from its M-Bus table; Core Matrix GPIOs from the vendor M-Bus table, cross-checked against M5Unified's CoreMatrix M-Bus pin table (they agree). The M5GFX `board_spec` JSON numbers positions 7–12 differently and puts 3V3 where pin 12 cannot be; I treat it as wrong. **[V]**

| Pin | Bottom2 uses it for | Core Matrix net | Effect |
|---|---|---|---|
| 1, 3, 5 | GND | GND | |
| 4 | PORT.B (white) | **G4** (ADC) | Bottom2 PORT.B becomes a G4/G6 port |
| 8 | **SK6812 DATA** (labelled GPIO25 on the Bottom2 schematic, Core2's pin 8) | **G5** | **The LED bars are on G5.** |
| 10 | PORT.B (yellow) | **G6** | |
| 12 | 3V3 (Bottom2 IMU, mic, pull-ups) | M5BUS_3V3_L0 (boost domain) | Bottom2 3V3 exists only while the PM1 boost is on |
| 15 / 16 | UART_RX / UART_TX → PORT.C | **G7 / G8** (RXD2/TXD2) | PORT.C becomes a UART on G7/G8 |
| 17 / 18 | IMU_SDA / IMU_SCL (+ pogo I2C) | **G0 / G1** (level-shifted bus copy) | Bottom2 IMU joins the main bus (§5.4) |
| 24 | I2S_LRCK (mic clock) | **G9 = BOOT strap** | Harmless: the mic clock pin is an input. Never drive G9 low at reset. |
| 26 | I2S_DIN (mic data) | G29 | Mic out of scope; leave G29 unconfigured |
| 28 | 5V (SK6812 supply) | 5V_INOUT (boost out / external in) | Bars powered only while the boost is on |
| 30 | BAT | **BAT** (Core Matrix VBAT) | **The Bottom2 cell becomes the Core Matrix's battery** |
| 7, 9, 11 | NC | microSD SPI (G27/G26/G25) | No conflict |
| 2, 6, 13, 14, 19–23, 25, 27, 29 | NC | | |

### 5.2 Battery and charging

- **One battery, one charger in practice.** The Bottom2's 500 mAh cell sits directly on the Core Matrix BAT net through pin 30, so the Core Matrix sees it exactly as if it were plugged into the MX1.25 connector. **[I]** from both pin maps; high confidence.
- **USB-C on the Core Matrix charges it** via the LGS4056 at the default **180 mA (≈0.36 C, ~3 h to full)**. **Do not select 650 mA** with only this cell: that is 1.3 C, above any normal standard-charge rating for a 500 mAh pouch. **[I]**
- **The Bottom2's TP4057 only charges from its pogo-pin dock.** The docs describe it as the dock charger; its VIN net isn't legible in the schematic text. If it were fed from M-Bus 5 V, the Core Matrix boost (running off the same battery) would charge the battery from itself in a lossy loop. On a Core2 the same topology would loop too, which is good evidence M5Stack wired VIN to pogo only. **[I], check §9.**
- **Two chargers on one cell** (USB on the Core Matrix *and* the Bottom2 on its dock) would both be CC/CV to 4.2 V in parallel. Unusual but not dangerous. Avoid it anyway. **[I]**
- **No battery protection IC** is identifiable on either schematic; the cell's own pouch PCM is the only protection. Combined with the PM1's 2.5 V default cut-off, that argues for raising `BATT_LVP` (§2.4). **[I]**
- **Reading the battery:** `M5.Power.getBatteryVoltage()` (mV, PM1 ADC), `getBatteryLevel()` (%), `isCharging()` (IOE1 IO8). With no battery, the VBAT node reading is meaningless; M5Unified has a presence heuristic and returns "unknown" until it decides. **[S]**
- **Power-off is real.** Double-click power turns off the PM1's 3.3 V DC/DC; the charger keeps working. Shutdown draw 8.21 µA ⇒ the cell self-discharges faster than the board drains it. **[D]**

### 5.3 The LED bars

- **Data on G5, 3.3 V logic into SK6812s powered at 5 V, no level shifter.** SK6812 V_IH is 0.7 × VDD = 3.5 V, so 3.3 V is technically out of spec. The identical arrangement (Core2 G25 at 3.3 V into the same Bottom2) ships in the M5GO kit and works, so expect it to work; if the first LED flickers, that is the cause. **[I]**
- **Power exists only when the PM1 boost (L0) is on.** M5Unified turns it on at boot. Turning it off (`M5.Power.M5pm1.setExtOutput(false)` or `M5PM1::setBoostEnable(false)`) cuts the bars *and* the Bottom2's 3V3 (IMU, mic). **[S]**
- **Idle current:** SK6812s draw roughly 0.5–1 mA each even when black, ~5–10 mA for ten at 5 V, plus boost losses. With the boost off, zero. **[I]** from typical SK6812 datasheets; measure.
- **Chain order is unknown.** Ten LEDs in one chain, five per side; whether index 0 is front-left, whether the second side runs in reverse: **[?]**. Map it on the bench and keep a `bar_index[side][pos]` table.
- **Driver:** see §6.

### 5.4 The IMU collision (the one real conflict)

The Bottom2's IMU sits on M-Bus 17/18, which on the Core Matrix are G0/G1, the main bus. The Core Matrix's own BMI270 is at **0x68**.

| Your Bottom2 | Its IMU | Its address | Result |
|---|---|---|---|
| A014-C | MPU6886 | 0x68 (MPU6886 default; strap not legible) | **Collides** with the internal BMI270: two different chips answering one address. Reads are wired-AND garbage. |
| A014-C-V13, SDO=0 | BMI270 | 0x68 | **Collides.** Writes configure both identically, so the chip-ID check *passes* and the failure is subtle. |
| A014-C-V13, SDO=1 | BMI270 | 0x69 | No collision, but **M5Unified probes 0x69 first**, so `M5.Imu` will bind to the *Bottom2's* IMU and apply the Core Matrix's axis rotation to it. Pin the address explicitly. |

There is no software fix for a true collision: both chips share an address and a bus. The only clean separation is to unpower the Bottom2 side (boost off), which also kills the bars. **Design rule: the timer must work fully without the IMU; IMU features switch on only after a boot-time check passes** (§9, check 3). **[I]**, structure verified against M5Unified `IMU_Class.inl`.

## 6. Driving the SK6812 bars without RMT

Recommended: **SPI2 as a waveform generator on G5.** 4 SPI bits per LED bit at **3.2 MHz** (312.5 ns per SPI bit):

| LED bit | SPI nibble | High time | Low time | SK6812 spec |
|---|---|---|---|---|
| 0 | `1000` | 312 ns | 938 ns | T0H 0.3 ± 0.15 µs ✔ |
| 1 | `1100` | 625 ns | 625 ns | T1H 0.6 ± 0.15 µs ✔ |

Ten LEDs × 24 bits × 4 = 960 SPI bits = **120 bytes**, then ≥ 80 µs low for latch (≥ 32 zero bytes). Byte order on the wire is **G, R, B**. With arduino-esp32: `SPI.begin(-1, -1, 5, -1)` (MOSI only), `SPISettings(3200000, MSBFIRST, SPI_MODE0)`, one `writeBytes()` per frame. Each symbol ends low, so any FIFO-refill gap holds the line low; gaps under ~50 µs don't latch. If gaps prove to be a problem, use the IDF `spi_master` with DMA for one continuous transaction, or Espressif's `led_strip` component's SPI backend.

Fallbacks, in order: I2S TX as waveform generator; cycle-counted bit-bang with interrupts masked for ~300 µs per update (fine for 10 LEDs).

The 3-bit/2.4 MHz encoding common online gives T1H = 833 ns, outside SK6812 spec. Don't use it.

## 7. Toolchain from the Mac

Recommendation: **arduino-cli with pinned versions**, scripted in the repo (a Makefile), so builds are reproducible and don't depend on IDE state. Arduino IDE 2 (already installed on this Mac) works for poking around.

| Item | Value |
|---|---|
| Board manager URL | `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json` (M5Stack's standard index; **confirm**, the CoreMatrix page links to its board-manager guide rather than stating the URL) |
| Board package | **M5Stack ≥ 3.3.10-a** (needed for the C61 target) |
| Board | **M5CoreMatrix** (find the FQBN with `arduino-cli board listall \| grep -i matrix`) |
| Libraries | **M5Unified ≥ 0.2.29**, **M5GFX ≥ 0.2.36**; optionally M5PM1 and M5IOE1 for direct PMIC access |
| Port | `/dev/cu.usbmodem*` (native USB-Serial-JTAG, Espressif VID 0x303A); no driver needed on macOS |
| Serial | Ensure "USB CDC On Boot: Enabled" so `Serial` routes to USB-Serial-JTAG (default in most C-series board defs; check) |
| Flashing | Plain upload works. If the chip is wedged, **long-press the power button** for download mode (PM1 holds BOOT). |
| Factory firmware | Uploading overwrites it. M5Burner can restore it if wanted. |

PlatformIO: the official `espressif32` platform is still on Arduino core 2.x; only the community `pioarduino` fork tracks core 3.x, and its C61 support is unconfirmed. Not worth the risk for v1. ESP-IDF (≥ 5.5) with M5Unified as a component is the escape hatch if Arduino gets in the way.

Minimal bring-up skeleton:

```cpp
#include <M5Unified.h>
void setup() {
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);                    // PM1, IOE1, LED rail, boost (M-Bus 5 V) on
  M5.Display.setBrightness(48);     // ≈ 4/16 duty
  M5.Display.setRotation(0);        // determine on bench
}
void loop() {
  M5.update();                      // polls PM1 for BtnA/B/C, BtnPWR
  if (M5.BtnB.wasClicked()) { /* ... */ }
}
```

## 8. Power budget (estimates, to be measured)

| Consumer | Estimate | Basis |
|---|---|---|
| ESP32-C61, CPU awake, radios off | 15–25 mA | [I] typical C6/C61 class |
| Same with automatic light sleep, waking ~15×/s | 2–5 mA average | [I] |
| Matrix, ~70 of 256 pixels at 3–4/16 duty | ~5–15 mA | [I] scaled from the 128 mA vendor full-load figure; very uncertain |
| SK6812 × 10 idle (boost on) | 5–10 mA at 5 V → ~8–15 mA from the cell | [I] |
| SK6812 lit (full teal gauge at 10 % cap → shrinking to 0) | ~17 mA at 5 V when full → ~12 mA average from the cell over a block; ~3× that at 25 % | [I] ≈12 mA per channel at full scale |
| PM1 + IOE1 + boost quiescent | ~1–2 mA | [I] |

Implication: on the 500 mAh cell, **roughly 9–12 h with bars on (10 % cap), 25–40 h with light sleep and the bars' 5 V rail off.** The rail, not the brightness, is the larger share of the bars' cost, which is why the design turns it off on battery. Enough for a working day either way; measure with a USB power meter (on USB) and by run-down test (on battery).

## 9. Bench checks before writing app code

Each is a five-minute sketch; together they retire every **[?]** above.

1. **Inventory.** Read the label on the Bottom2 (A014-C vs A014-C-V13). Plug in, confirm `/dev/cu.usbmodem*` appears.
2. **I2C scan, boost on vs off.** Expect 0x4F, 0x68, 0x6E, 0x72 (0x72 won't show in a read-probe scan; probe with a zero-length write). Then `M5.Power.M5pm1.setExtOutput(false)` and rescan. Anything that disappears is on the Bottom2.
3. **IMU collision test.** With the boost on, read WHO_AM_I/CHIP_ID at 0x68 and 0x69, then stream accelerometer data while tilting. Clean, gravity-consistent data at a known address → IMU features allowed. Noisy, quantised or nonsensical data → collision; IMU features stay off whenever the bars are powered.
4. **Bars.** SPI driver on G5, light index 0..9 in turn, record the physical position of each into `bar_index`. Check the first LED for flicker (3.3 V logic margin).
5. **Matrix orientation.** Draw an arrow; record which rotation puts "up" away from the buttons, and where the USB-C port and buttons sit relative to it.
6. **Power button.** Confirm single = reset, double = off, long = download; then `setSingleResetDisable(true)` and confirm single click arrives as `M5.BtnPWR.wasClicked()`.
7. **Battery.** Report `getBatteryVoltage()`, `isCharging()` on USB and off; confirm charging stops at ~4.2 V at 180 mA.
8. **Pogo loop check (only if a dock is ever used).** On battery, measure battery current with the boost on vs off. A difference far beyond the bars' idle draw would mean the TP4057 is fed from M-Bus 5 V.
9. **Timebase drift.** Run a 30-minute timer in light sleep against the Mac clock; record the error.
