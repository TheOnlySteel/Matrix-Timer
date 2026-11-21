# Firmware v1: build, flash, test

The firmware is in [`firmware/MatrixTimer`](../firmware/MatrixTimer). It implements [design.md](design.md) on the hardware described in [hardware.md](hardware.md) and measured in [bench-results.md](bench-results.md).

## Build and flash

```bash
make setup            # once: pinned core + libraries into ./.arduino
make test             # host tests for the pure modules (Clock, Face)
make flash-firmware   # build + flash over USB-C
make log SECS=60      # watch the serial console (115200)
```

## Modules

There is one Arduino sketch, and every module is a header (one translation unit). There is one loop and no RTOS tasks of our own.

| File | Module (design.md §7) | Notes |
|---|---|---|
| `clock.h` | Clock | `remaining = duration − (now − started − paused_total)`. Pure, host-tested. |
| `face.h` | Face | Port of `tools/mockups/render.py`: `ticksLeft` (ceil), `grid`, `durationFrame`, `peekFrame`, plus the settings frame. `SmoothGrid` advances the shown LED only at blink-cycle starts (1 s on / 0.5 s off). In the off phase before a hand-over both LEDs go dark together, so hand-overs never stutter. Pure, host-tested. |
| `matrix.h` | Matrix | Frame → TM1680 at M5GFX rotation 0. The firmware rotates in software (`up` 0–3), so the orientation is exactly the bench-measured one. It pushes only on change. `applyDuty(n)` sets TM1680 duty n/16. |
| `bars.h` | Bars | IDF `spi_master` on G5 with SCLK unassigned, 3.2 MHz, 4-bit symbols, GRB. Gamma 2.2, cap 5/10/25 %, hard cap 64/255. The U-chain map (4/5 at the USB-C end, desk-confirmed) follows the current orientation, and a 2 s cross-fade is implemented as easing (τ 0.6 s). |
| `input.h` | Input | `M5.update()` plus a 50 ms lockout per key, holds, chords, and simulated presses from the console. |
| `tilt.h` | Tilt | BMI270 at 0x68. It is initialised with the boost **off**, and after every boost-on it writes 0xFF into 0x0C..0x11 (the MPU6886 workaround). It reads only the 6-byte accel burst, and switches itself off after 3 s of implausible readings. |
| `persist.h` | Persist | 16-byte session record (with CRC8) in PM1 RTC RAM at 0xA0, rewritten on every LED tick and every state change. Settings and last duration live in NVS (`mt`). |
| `power.h` | Power | VIN > 4.5 V counts as USB. LVP 3.3 V, `SINGLE_RST_DIS` set every boot, the boost follows the Bars setting (Auto: on with USB), and the 10-min idle power-off in Set on battery. Never touches the PM1 LDO. |
| `sound.h` | Sound | IOE1 IO2 is the buzzer enable (`M5IOE1_Class::gpio2`, which is 0-based **1**: a raw `2` would be IO3 = CHG_PROG, i.e. 650 mA). PWM is channel 3 (IO11) at 2.7 kHz, and IO11 must be set as an output, or the PWM never reaches the pin. |
| `MatrixTimer.ino` | App | The state machine of design.md §5.1, settings menu, serial console. |

## Serial console

115200 baud on the USB-C port (`make log`, or any terminal). Each command is one line:

| Command | Does |
|---|---|
| `a` `b` `c` | Short press of A / B / C |
| `A` `B` `C` | Hold: A 1 s (repeat), B 1.7 s (cancel / repeat), C 1.2 s (+5 min) |
| `X` | A+C held 2.3 s (settings) |
| `p` | Power-button click (matrix on/off) |
| `f` | Dump the current frame as 16 lines of `#`/`.`, with state, orientation and duty |
| `s` | Status: state, duration, remaining, ticks, USB, boost, battery, tilt, settings |
| `wN` | Warp the clock forward N seconds (testing only, not saved) |
| `r` | Raw accelerometer (g), pose and down edge |
| `t` | Three test chirps |

State changes are logged as `STATE A -> B`.

## Behaviour notes

- **Pause** shows the grid steady at duty 1/16, with one dim amber bar LED.
- **On battery** the matrix is capped at duty 2/16, whatever the brightness setting (pause stays at 1/16).
- **Lay flat to pause** fires only on a real transition into "flat" (dwell 1 s). A block restored after a reset is not paused just because the device happens to be lying down.
- **Peek** shows whole minutes rounded up, and seconds under one minute. C-hold (+5 min) also peeks, so the new total is visible.
- **Done**: a centred 4×4 square on the matrix (at 1/16) and the red bars alternate, at 2 Hz with a 3-chirp burst at the finish and single chirps at 15, 30, 45 and 60 s, then at 0.5 Hz until dismissed. Flip-to-repeat needs the opposite edge down for 0.8 s.
- **Settings** (A+C 2 s in Set) items are `M` matrix brightness 1–8, `B` bars A/1/0 (Auto/On/Off), `I` bar intensity 5/10/25 %, `S` sound 1/0, and `O` orientation A/0–3. B advances, and B on the last item saves and exits.
- **Bars under Auto** run only on USB. On battery the 5 V boost is off, which also removes the IMU collision. The battery gauge in Set is therefore only visible on USB.

## Not in v1 (yet)

- **Light sleep on battery.** The loop runs awake with a 5 ms delay. Battery life is roughly the "bars off" figure of hardware.md §8 without the light-sleep saving. Next step: automatic light sleep between frame edges, woken on the PM1 IRQ (G2).
- The low-battery blink on the bars is only visible when the bars are powered (On, or Auto on USB, where the battery isn't low anyway). A matrix-side indicator would be better.

## Tilt axis mapping

`tilt.h` maps raw BMI270 axes to panel axes: panel x (toward the right edge, opposite USB-C) = raw y, and panel y (toward the bottom edge, opposite the buttons) = raw x. Flat with the matrix up reads raw z = +1 g. On the desk (21 Nov) Steel stood it on each edge, and auto-orientation turned the face upright every time. The log showed raw y = +0.99 g with the USB-C edge down, giving `down=3`, `up=1`.
