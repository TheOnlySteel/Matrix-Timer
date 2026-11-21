#!/usr/bin/env python3
"""Log the CoreMatrix's USB serial output with timestamps, optionally sending commands.

usage: serlog.py PORT SECS OUTFILE [CMD ...]

Each CMD is either "text" (sent 1.5 s after opening) or "T:text" (sent T seconds
after opening). A newline is appended to every command.

While running, any line appended to OUTFILE + ".ctl" is sent to the board too,
so commands can be injected mid-run from another shell:
    echo s >> bench/logs/check3.txt.ctl

The port is opened with DTR and RTS asserted *before* open(): on the ESP32
USB-Serial-JTAG, opening with both released pulses reset.
"""
import os
import sys
import time

import serial


def main() -> int:
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    port, secs, out = sys.argv[1], float(sys.argv[2]), sys.argv[3]
    sched = []
    for c in sys.argv[4:]:
        t, sep, text = c.partition(":")
        if sep and t.replace(".", "", 1).isdigit():
            sched.append((float(t), text))
        else:
            sched.append((1.5, c))
    sched.sort()

    os.makedirs(os.path.dirname(out) or ".", exist_ok=True)
    ctl = out + ".ctl"
    open(ctl, "w").close()
    ctl_pos = 0

    ser = serial.Serial()
    ser.port = port
    ser.baudrate = 115200
    ser.timeout = 0.05
    ser.write_timeout = 1  # a stale port after unplug/replug must not hang the logger
    ser.dtr = True
    ser.rts = True
    ser.open()

    t0 = time.monotonic()
    buf = b""
    with open(out, "a", buffering=1) as f:
        def emit(line: str) -> None:
            stamp = f"[{time.monotonic() - t0:7.2f}] "
            f.write(stamp + line + "\n")
            print(stamp + line, flush=True)

        emit(f"# serlog {port} {time.strftime('%Y-%m-%d %H:%M:%S')}")
        while time.monotonic() - t0 < secs:
            now = time.monotonic() - t0
            while sched and sched[0][0] <= now:
                _, text = sched.pop(0)
                try:
                    ser.write((text + "\n").encode())
                    emit(f">> {text}")
                except serial.SerialException as e:
                    emit(f"# write failed ({e}): {text}")
            with open(ctl) as c:
                c.seek(ctl_pos)
                for text in c.read().splitlines():
                    ser.write((text + "\n").encode())
                    emit(f">> {text}")
                ctl_pos = c.tell()
            try:
                chunk = ser.read(4096)
            except serial.SerialException as e:  # board reset / port vanished
                emit(f"# port error: {e}; reopening")
                ser.close()
                for _ in range(100):
                    time.sleep(0.1)
                    try:
                        ser.open()
                        break
                    except serial.SerialException:
                        pass
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                emit(line.decode(errors="replace").rstrip("\r"))
        if buf:
            emit(buf.decode(errors="replace"))
        emit("# end")
    os.remove(ctl)
    return 0


if __name__ == "__main__":
    sys.exit(main())
