# Bench results: CoreMatrix + M5GO Bottom2

Run on 21 November 2026 on Steel's Mac, with the CoreMatrix stacked on the Bottom2 and plugged in over USB-C. The checks are the ones in [hardware.md §9](hardware.md#9-bench-checks-before-writing-app-code). Each one has a sketch under [`bench/`](../bench) and a raw serial log under [`bench/logs/`](../bench/logs). Lines quoted below are copied from those logs. Where something needed eyes on the hardware, Steel looked and reported, and that is marked **(Steel)**.

## Summary

| # | Question | Answer |
|---|---|---|
| 1 | Board, port | ESP32-C61 rev v1.1, 16 MB flash, `/dev/cu.usbmodem101`, USB **303A:1001** ("USB JTAG_serial debug unit") |
| 2 | I2C devices | 0x4F, 0x68, 0x6E, 0x72. **Nothing at 0x69.** The address list is the same with the boost on or off. |
| 3 | Bottom2 IMU | **An MPU6886 at 0x68**, not a BMI270. It **collides** with the CoreMatrix BMI270 whenever the boost is on. Boost off: clean. Boost on: garbage, unless the workaround in §3.4 is applied. |
| 4 | Bar chain | 0→4 runs down the right bar from the USB end, 5→9 runs up the left bar (a U). The bars **go dark with the boost off, even on USB**. |
| 5 | Orientation | `setRotation(0)` puts "up" toward the **three buttons**. The USB-C edge is the panel's left. Buttons are in A-B-C order along their edge. |
| 6 | Power button | Factory: single click = reset (a PM1 power cycle of the ESP), double click = off, long press = download mode. `SINGLE_RST_DIS` works and survives ESP resets, and clicks then arrive as `BtnPWR.wasClicked()`. |
| 7 | Battery | 4.16 V, "not charging" (full) on USB. It ran on the Bottom2 cell with no reset when unplugged, and charging resumed on replug. M5Unified reports battery voltage as −1 (unknown) for the first ~30 s after boot. |

What it means for the design is in [§ Design consequences](#design-consequences).

## Toolchain (hardware.md §7)

`make setup`, then `make flash SKETCH=bench/<check>` and `make log`. The Makefile pins everything, and arduino-cli's data and user directories live in `./.arduino` (git-ignored), so nothing in `~/Library/Arduino15` is touched.

| Item | Pinned |
|---|---|
| Board index | `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index_cn.json` |
| Core | `m5stack:esp32@3.3.10-a` |
| FQBN | `m5stack:esp32:m5stack_corematrix:CDCOnBoot=cdc` |
| Libraries | M5Unified 0.3.0, M5GFX 0.3.0, M5PM1 1.0.7, M5IOE1 1.0.9 (all ≥ the minimums in §7) |
| Serial logger | `tools/serlog.py` (pyserial 3.5 in `.venv`). It opens the port with DTR/RTS asserted so the open doesn't reset the board, and `echo cmd >> <log>.ctl` sends a command mid-run. |

**Contradicts hardware.md §7:**

- The board index URL in §7 (`package_m5stack_index.json`, last modified 19 Oct) stops at core **3.3.10**. That version has no `esp32c61-libs` tool, and the build fails at the bootloader step with `'{runtime.tools.esp32c61-libs.path}/bin/bootloader_qio_80m.elf' does not exist`. **3.3.10-a** is published only in the **`_cn`** index (modified 6 Nov, launch day), which docs.m5stack.com lists as the China mirror. Use that index until the global one catches up.
- The board ID is `m5stack_corematrix` in 3.3.10-a (it was `m5stack_core_matrix`, hidden, in 3.3.10). `board listall | grep -i matrix` finds it.
- Arduino's `SPIClass::begin()` refuses `sck = -1` (`spiAttachSCK` fails), so the `SPI.begin(-1, -1, 5, -1)` in §6 won't work as written. The bars sketch uses the IDF `spi_master` instead (see §4).

**Factory firmware:** the full 16 MB was read back before the first flash and checked with `esptool verify-flash` (digest matched). It is kept off-repo at `~/Developer/Matrix-Timer-backup/corematrix-factory-2026-11-21.bin` (sha256 `652f7761…7dde`). A single 16 MB `read-flash` dies with "serial noise". It has to be read in 1 MB chunks, and the sector at 0x312000 reads only with `--no-stub`.

## 2. I2C scan, boost on vs off

Sketch [`bench/check2_i2c_scan`](../bench/check2_i2c_scan), log [`check2_i2c_scan.txt`](../bench/logs/check2_i2c_scan.txt).

```
SCAN boost-on     write-probe(4): 0x4f 0x68 0x6e 0x72
SCAN boost-on     read-probe (3): 0x4f 0x68 0x6e
setExtOutput(false) -> 1, readback=0
SCAN boost-off    write-probe(4): 0x4f 0x68 0x6e 0x72
SCAN boost-off    read-probe (3): 0x4f 0x68 0x6e
```

- As hardware.md predicted, the TM1680 (0x72) ACKs only a write probe.
- Nothing appears or disappears with the boost, so the Bottom2 adds **no new address**: its IMU hides behind 0x68 (§3).
- Gotcha: `M5.In_I2C.start()` returns true for every address, because M5GFX queues the transaction and reports the address ACK from `stop()`. Probe with `start(a,false,f) && stop()` (that is `M5.In_I2C.scanID(a)`).

## 3. IMU address and collision

Sketch [`bench/check3_imu`](../bench/check3_imu). `cfg.internal_imu = false` keeps M5Unified away from 0x68, and both addresses are driven by hand.

### 3.1 The collision

Log [`check3_imu_identify.txt`](../bench/logs/check3_imu_identify.txt):

```
BOOST ON : ...
WHO 0x68: reg00(BMI270 CHIP_ID,0x24)=0x00 ...                 <- CHIP_ID wrong
INIT 0x68: begin -> spec=0  CHIP_ID=0x00 ...                  <- M5Unified init fails
  SENSORTIME 0x68: backwards=0 stalls=40 of 40 ... seq: 0 0 0 0 ...
BOOST OFF: ...
WHO 0x68: reg00(BMI270 CHIP_ID,0x24)=0x24 ...
INIT 0x68: begin -> spec=3  CHIP_ID=0x24 INTERNAL_STATUS=0x01 ACC_RANGE=2 PWR_CTRL=0x0e
  SENSORTIME 0x68: backwards=0 stalls=0 of 40, span=2077 ticks ... seq: 5057 5108 5160 ...
ACC 0x68 boost=0 raw=    -3     19   4060  g=-0.001 +0.005 +0.991 |g|=0.991
BOOST ON : ...
ACC 0x68 boost=1 raw= 24322      1   2704  g=+1.484 +0.000 +0.165 |g|=1.494
```

With the boost on, every read of 0x68 is the **wired-AND** of two chips. The BMI270's CHIP_ID 0x24 reads as 0x00, its free-running sensor clock reads as a constant 0, and the accelerometer returns frozen, non-physical values. With the boost off (Bottom2 3V3 unpowered), the CoreMatrix BMI270 is alone and reads cleanly. **M5Unified's own `M5.Imu.begin()` fails** with the boost on, because CHIP_ID ≠ 0x24. Its 0x69-first probe finds nothing.

### 3.2 What the second chip is: an MPU6886

A BMI270 would AND with another BMI270 to give 0x24, so the second chip isn't one. To identify it without unpowering anything, the sketch writes 0xFF to registers 0x72 and 0x75 and reads them back. On a BMI270 these are OFFSET_1 and OFFSET_4, plain R/W bytes. On an MPU6886 they are FIFO_COUNTH (read-only) and **WHO_AM_I** (read-only, 0x19).

```
IDENT boost=0 ... after writing 0xFF: r72=0xff r75=0xff -> all BMI270
IDENT boost=1 ... after writing 0xFF: r72=0x00 r75=0x19 -> second chip looks like MPU6886 (WHO_AM_I 0x19)
```

0x19 is exactly the MPU6886 WHO_AM_I. **Your Bottom2 has an MPU6886 at 0x68, so it behaves like the older A014-C.** hardware.md §0 records it as an A014-C-V13 (BMI270), confirmed from the label. Either the label is wrong, or this v1.3 unit carries an MPU6886. The table in hardware.md §5.4 already covers this case: the "A014-C, MPU6886, 0x68: collides" row.

### 3.3 Motion (Steel moved it, boost toggled every 4 s)

Log [`check3_imu_motion.txt`](../bench/logs/check3_imu_motion.txt). Means over each 4 s window:

```
t=  89.4 boost=0  g=(-0.00,+0.00,+0.99) |g| 0.99/1.00   flat, matrix up
t= 106.8 boost=0  g=(-0.49,-0.07,+0.85) |g| 0.97/1.02   tilted
t= 133.7 boost=0  g=(-1.00,+0.01,-0.02) |g| 0.99/1.00   standing on an edge
t= 151.1 boost=0  g=(+0.00,-0.01,-1.01) |g| 1.00/1.01   flat, matrix down
t= 129.3 boost=1  g=(+1.25,+0.18,-0.73) |g| 1.25/2.57   (any pose: garbage)
t= 155.4 boost=1  g=(+0.04,+1.80,-1.33) |g| 2.24/2.69
```

Boost off: gravity-consistent in every pose, |g| 0.97–1.03. Boost on: |g| anywhere from 0.17 to 2.77. (Axes are the BMI270's raw axes, before M5Unified's CoreMatrix rotation.)

### 3.4 Workaround: make the MPU6886 transparent

Because a read returns BMI270 AND MPU6886 for the same register number, fill the MPU6886's copies of the BMI270 accel registers with 1s. The BMI270's accel data is 0x0C–0x11. On the MPU6886, 0x0D–0x0F are SELF_TEST_X/Y/Z_ACCEL (R/W) and 0x0C/0x10/0x11 are reserved. Write 0xFF to all six (the BMI270 ignores writes to its read-only data registers), and from then on `x AND 0xFF = x`.

Sequence: boost off → init the BMI270 (so the 8 KB config upload never reaches the MPU6886) → boost on → write 0xFF to 0x0C..0x11 → read accel. Logs: [`check3_imu_transparent_rest.txt`](../bench/logs/check3_imu_transparent_rest.txt) and [`check3_imu_transparent_motion.txt`](../bench/logs/check3_imu_transparent_motion.txt).

```
ACC 0x68 boost=0 raw=   -74     -1   4061  ... |g|=0.992     reference, boost off
BOOST ON ... wrote 0xFF to 0x0C..0x11 (burst ok=1)
ACC 0x68 boost=1 raw=   -54    -12   4067  ... |g|=0.993     boost on, bars' rail live
```

**(Steel)** ran the motion routine with the boost **on** the whole time (7-minute window, "GO" on the matrix). Means per 5 s:

```
t=  90s  g=(-0.01,+0.01,+0.99) |g| 0.99-0.99    flat, matrix up
t=  95s  g=(-0.24,+0.01,+0.93) |g| 0.97-1.04    tilting
t= 105s  g=(+0.27,-0.01,+0.91) |g| 0.97-1.04    tilting the other way
t= 110s  g=(-0.01,-0.31,+0.92) |g| 0.95-1.03
t= 115s  g=(-0.89,+0.01,+0.12) |g| 0.91-1.15    standing on an edge
t= 125s  g=(+0.01,-0.00,-1.01) |g| 1.00-1.01    flat, matrix down
t= 135s  g=(-0.01,+0.01,+0.99) |g| 0.99-1.00    flat, matrix up again
```

All 1,384 boost-on samples had |g| between 0.80 and 1.20, and the only excursions were while it was being moved. That is the same quality as boost-off (compare §3.3). **The workaround works.**

Limits:
- It covers **only those six bytes**. Any other BMI270 register read with the boost on is still ANDed: STATUS, INT_STATUS, ACC_RANGE (it collides with the MPU6886's TEMP_OUT_L, which is why the sketch caches the range at init), and the feature-engine outputs. Read the accel burst blind, at a fixed rate, with no data-ready check.
- It has to be **redone after every boost-on**, because the MPU6886 powers up with factory self-test values.
- It relies on undocumented MPU6886 behaviour (keeping 0xFF in reserved registers 0x0C/0x10/0x11). Verify the six bytes on every boost-on by checking that a burst read gives |g| between 0.8 and 1.2 g at rest. If that check fails, fall back to "IMU only with the boost off".

### 3.5 Don't isolate the Bottom2 chip by switching off the PM1 LDO

An earlier attempt to identify the second chip switched off the PM1 LDO (3V3_L1, which powers the CoreMatrix BMI270). The unpowered BMI270 then **clamped the I2C bus**. Nothing ACKed, `setLDOOutput(true)` itself failed (`LDO ON : set=0`), and after an ESP reset M5Unified couldn't even detect the board (`board=0`). Logs: [`check3_imu_ldo_off_hang.txt`](../bench/logs/check3_imu_ldo_off_hang.txt), [`check2_after_ldo_off.txt`](../bench/logs/check2_after_ldo_off.txt). The PM1 keeps the setting across ESP resets, and double-clicking the power button did nothing. Recovery took lifting the CoreMatrix off the Bottom2 with USB unplugged, so the PM1 lost all power. **Firmware must never turn the LDO off while the ESP is running.**

## 4. SK6812 bars on G5

Sketch [`bench/check4_bars`](../bench/check4_bars), log [`check4_bars.txt`](../bench/logs/check4_bars.txt). The SPI waveform is the one from hardware.md §6 (3.2 MHz, 4 SPI bits per LED bit, `1000`/`1100`, GRB order, 100 µs of low before and after), sent with the IDF `spi_master` on SPI2, MOSI = G5, **SCLK unassigned (−1)**, DMA, as one transaction. Brightness is clamped to 25/255 in the encoder.

**(Steel)** With the device flat, matrix up, USB-C pointing away:

```
        USB-C (far edge)
   9 ┐                ┌ 0
   8 │   left bar     │ 1   right bar
   7 │    (5..9 up)   │ 2   (0..4 down)
   6 │                │ 3
   5 ┘                └ 4
        (near edge)
```

`bar_index` (side, position from the USB end) is right = 0,1,2,3,4 and left = 9,8,7,6,5. Steel gave the endpoints (0 right top, 4 right bottom, 5 left bottom, 9 left top). The positions in between are taken as sequential, and every index lit. No flicker on the first LED was reported, so the 3.3 V data into 5 V SK6812s works as predicted.

**Boost test (Steel):** with all 10 lit, switching the boost off ("B-") turned **all bars dark**, on USB. The bars' 5 V exists only while the PM1 boost runs. The PM1's 5VOUT ADC reading is not a usable indicator of this: it read ~5.36 V with the boost off and 0–390 mV just after switching it on, so it seems to lag or read the input side.

## 5. Matrix orientation

Sketch [`bench/check5_orientation`](../bench/check5_orientation). At `setRotation(0)` it draws an arrow toward the panel's top edge and a single pixel at the top-left.

**(Steel)** Flat, USB-C away: the arrow points **right**, toward **the edge with the three buttons**, and the dot is in the **far-right** corner (the corner shared by the USB edge and the button edge).

- **Rotation 0: up = the button edge.** The USB-C edge is the panel's left, the near edge is its right, and the image is not mirrored.
- Standing on the buttons (buttons down) needs rotation 2. Standing with USB-C down needs a quarter turn, which is 1 or 3 depending on M5GFX's sign convention. That wasn't checked on the bench, and it doesn't matter if auto-rotate drives it from the IMU.
- Buttons: Steel pressed the three "left to right as you see them", and they logged `BTN A`, `BTN B`, `BTN B`, `BTN C`, so they are in A-B-C order along the edge. Most likely, with the buttons at the top (rotation 0 upright), A is on the left (the USB-C end) and C on the right. That depends on how Steel was looking at the edge, so **confirm with one press of A** before hard-coding it. B logged twice for one press, possibly a bounce. Watch for that in the button handling.

## 6. Power button

Sketch [`bench/check6_power_button`](../bench/check6_power_button), log [`check6_power_button.txt`](../bench/logs/check6_power_button.txt). A boot counter in PM1 RTC RAM byte 0xA0 is shown on the matrix and printed with the ESP reset reason and the PM1 wake source.

Factory button config: `BTN_CFG_1=0x2a` (DL_LOCK=0, double-click gap 250 ms, long press 2 s, click 250 ms, SINGLE_RST_DIS=0), `BTN_CFG_2=0x00` (double-click off enabled).

| Action (Steel, on USB) | Result |
|---|---|
| Single click | Port drops, `BOOT #2 ... esp_reset_reason=1 wake_src=0x08`. Reset reason 1 is POWERON, not a software reset: the PM1 power-cycles the ESP. Wake bit 3 is "reset button". |
| Double click | Off (matrix dark, port gone) **even with USB plugged in**. A click powers it back on: `BOOT #3 ... wake_src=0x0c` (power button + reset button bits). |
| Long press ~3 s | Download mode (the ROM loader answers `esptool --before no-reset`). An RTS "hard reset" from esptool **lands back in download mode**. `esptool --after watchdog-reset` gets out. |
| `BTN_CFG_1 \|= 1` (0x2b), then single clicks | The **first** click still reset once (`BOOT #5 ... wake_src=0x0c`, with 0x2b read back after that boot, so the bit survived). Every later click gave `EVENT BtnPWR.wasClicked()` with no reset. |

- Single-click-reset disable works and persists across ESP resets (it lives in the PM1). It does not survive PM1 power loss, so **set it at every boot**.
- The one reset right after setting the bit isn't explained. It could be a PM1 quirk, or a stray double press. Either way, setting the bit at boot covers it.
- The bit was cleared again after the test (`BTN_CFG_1=0x2a`), so the board is back to factory behaviour.
- The design's persistence through resets (PM1 RTC RAM) is supported: the 0xA0 counter survived both kinds of button reset and the double-click power-off.
- Earlier in the session, before this sketch, Steel saw the power button "do nothing". That was while the I2C bus was clamped (§3.5). The PM1 itself kept running, and only unstacking it reset everything.

## 7. Battery and charging

Sketch [`bench/check7_battery`](../bench/check7_battery), log [`check7_battery.txt`](../bench/logs/check7_battery.txt). It is read-only: it never calls `setChargeCurrent()`, so the charger stays at its default (180 mA by hardware.md; not measured). While off USB it records every 5 s into a RAM ring, printed after replug (`REC` lines).

```
BAT t=2s   level=-1%  batt=-1mV   isCharging=0 chargeState=-1 | PM1 VBAT=4162mV VIN=5252mV src=5
BAT t=32s  level=100% batt=4164mV isCharging=0 chargeState=2  | PM1 VBAT=4164mV VIN=5252mV src=5
REC t=55s  VBAT=4164 VIN=5252 isCharging=0      <- on USB
REC t=65s  VBAT=4148 VIN=2    isCharging=0      <- unplugged (Steel): running on the Bottom2 cell
REC t=135s VBAT=4130 VIN=2    isCharging=0
REC t=140s VBAT=4188 VIN=5218 isCharging=1      <- replugged: charging resumes
BAT t=172s level=100% batt=4188mV isCharging=1 chargeState=1
```

- **The Bottom2's cell is the CoreMatrix's battery** (hardware.md §5.2 confirmed): unplugging USB didn't reset the ESP (the RAM ring survived), and the PM1 reads the cell on VBAT.
- At 4.16 V on USB the charger reported **not charging** (terminated, full). Replugging after 80 s on battery (4.13 V under load) **restarted charging** (`isCharging=1`, VBAT up to 4.19 V while charging). Termination at ~4.2 V wasn't observed: the cell started full, and a full run-down and recharge didn't fit in this session.
- `src=5` = VIN | battery (PM1 `pwr_src_t`). VIN reads 0–76 mV unplugged and ~5.25 V on USB, so **VIN > 4.5 V is a clean "on USB" test** for the bars' Auto setting.
- **For ~30 s after boot, M5Unified reports battery level and voltage as −1** (`chargeState=-1`, undetermined) while its presence heuristic settles. The raw `M5pm1.getBatteryVoltage()` is valid immediately. The firmware's battery and Auto logic should use the PM1 value, or wait for the M5Unified value to become valid.
- The sketch's raw `CHG_STAT(IOE1 G8)` column read 0 throughout, even when M5Unified correctly said not charging. My `getIOExpander(0).digitalRead(8)` call evidently doesn't read that pin, so ignore that column and use `M5.Power.isCharging()`.
- Under the boost-on load with the bars dark, the cell sagged ~30 mV in 80 s from full. That isn't a current measurement (§8's run-down test still applies).

## Design consequences

1. **Lay-flat-to-pause (design.md §5.4).** The Bottom2 IMU is at 0x68 and collides, which is the bad row of the table. But the §3.4 workaround held through a full motion routine with the boost on and the bars' rail live. Recommendation:
   - **On battery, bars Auto/off (boost off):** read the CoreMatrix BMI270 normally. This is verified clean in every pose.
   - **On USB with bars on (boost on):** use the transparent-MPU6886 read. After each boost-on, write 0xFF to 0x68 registers 0x0C–0x11, then read only the 6-byte accel burst at 0x0C. Self-check it at rest (|g| 0.8–1.2 g). If the check fails, fall back to B-button pause and the manual orientation setting, as design.md already says.
   - Always initialise the BMI270 **with the boost off**, then turn the boost on. With the boost on, `BMI270_Class::begin()` fails (CHIP_ID reads 0x00), and its config upload would scribble over the MPU6886.
   - **Don't use `M5.Imu`** on this stack. Use `cfg.internal_imu = false` and our own 0x68 driver. M5Unified's probe either fails (boost on) or, booted with the boost off, binds an IMU that breaks as soon as the boost turns on.
   - Use only accel. Making the gyro bytes (0x12–0x17) transparent wasn't tried, and the design doesn't need gyro.
2. **Bars need the boost, even on USB.** "B-" was dark, so boost-off-for-IMU and bars-on are mutually exclusive without the workaround. With it, they coexist.
3. **`bar_index`** for the gauge, with position 0 at the USB end: right = {0,1,2,3,4}, left = {9,8,7,6,5}. A gauge that empties symmetrically from the far end lights `right[i]` and `left[i]` together.
4. **Orientation.** Rotation 0 has its top at the button edge. If the frame stands on the USB edge (cable out of the bottom), content-up is the edge opposite USB, a quarter turn from rotation 0. If auto-rotate runs (it can, see 1), this only sets the default. The BMI270 raw axes read x = −1 g in the standing pose Steel used, z = +1 g flat matrix-up, and z = −1 g face-down. design.md's "Z > 0.8 g → flat" rule holds on raw axes. Check the sign of the in-plane axes when mapping them to the four rotations.
5. **Power button.** Set `SINGLE_RST_DIS` at every boot to free a single click as an input. Keep double-click = off as the factory power-off. It works on USB too, so a double click on the desk powers the timer down, and the RTC-RAM persistence brings the block back on the next power-on. Consider `DL_LOCK` so a long press can't drop the timer into download mode mid-block. Recovery from download mode needs a watchdog reset, not an RTS reset.
6. **Never turn off the PM1 LDO** while running (§3.5).
7. **Toolchain:** use the `_cn` board index until the global one carries 3.3.10-a.

**Things in hardware.md that this contradicts:**
- §0 / §4: the Bottom2 reports an **MPU6886**, not a BMI270, despite the v1.3 label.
- §5.4 table: the case that applies is "MPU6886 at 0x68: collides". Also, M5Unified's `M5.Imu` doesn't silently bind the wrong IMU: with the boost on, it fails to find any IMU.
- §6: `SPI.begin(-1, -1, 5, -1)` fails in arduino-esp32 3.3 (SCK −1 is rejected). Use the IDF `spi_master` with `sclk_io_num = -1`.
- §7: the board index URL and board ID (see Toolchain above).
- §2 table: the charger status line is described in M5Unified as the **AW32901**'s CHG_STAT (hardware.md says LGS4056 for the charger). Not resolved here, and it doesn't affect the design.
- §5.3: "Power exists only when the PM1 boost is on" is confirmed, including on USB. The PM1's 5VOUT ADC is not a reliable readback of it.

Check 8 was skipped as instructed. Check 9 (timebase drift) wasn't run.
