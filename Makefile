# Matrix-Timer build for the M5Stack CoreMatrix (ESP32-C61), via arduino-cli.
#
# Everything arduino-cli installs (board core, toolchain, libraries) goes into
# ./.arduino, not ~/Library/Arduino15, so this project's pinned versions never
# clash with other sketches on the same Mac.
#
#   make setup                      install the pinned core + libraries (once)
#   make firmware                   build the timer (firmware/MatrixTimer)
#   make flash-firmware             build + flash the timer
#   make test                       host tests for the pure modules
#   make build SKETCH=bench/check2_i2c_scan
#   make flash SKETCH=bench/check2_i2c_scan [PORT=/dev/cu.usbmodem101]
#   make log   [SECS=20] [OUT=bench/logs/x.txt] [SEND="r"]
#   make bench-all                  compile every bench sketch (no upload)
#   make port                       show the detected port and its USB IDs

# ---- pinned versions --------------------------------------------------------
# Core 3.3.10-a (the first with the esp32c61-libs tool, needed for the C61) is
# only in M5Stack's *_cn* index as of 21 Nov 2026; the global index stops at
# 3.3.10, which lacks esp32c61-libs and fails at the bootloader step.
BOARD_URL     := https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index_cn.json
CORE          := m5stack:esp32@3.3.10-a
LIBS          := M5Unified@0.3.0 M5GFX@0.3.0 M5PM1@1.0.7 M5IOE1@1.0.9

# CoreMatrix FQBN, from `arduino-cli board listall | grep -i matrix` (core 3.3.10-a).
# CDC-on-boot is the board default (Serial = USB-Serial-JTAG).
FQBN          := m5stack:esp32:m5stack_corematrix:CDCOnBoot=cdc

# ---- environment ------------------------------------------------------------
ARDUINO_CLI   ?= arduino-cli
export ARDUINO_DIRECTORIES_DATA      := $(CURDIR)/.arduino/data
export ARDUINO_DIRECTORIES_USER      := $(CURDIR)/.arduino/user
export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS := $(BOARD_URL)
export ARDUINO_LIBRARY_ENABLE_UNSAFE_INSTALL := false

# Native USB-Serial-JTAG, Espressif VID:PID 303A:1001.
PORT          ?= $(firstword $(wildcard /dev/cu.usbmodem*))
SKETCH        ?= bench/check2_i2c_scan
BUILD_DIR      = .build/$(notdir $(SKETCH))
PY            := .venv/bin/python
SECS          ?= 20
OUT           ?= bench/logs/$(notdir $(SKETCH)).txt
SEND          ?=

FW            := firmware/MatrixTimer

.PHONY: setup venv build flash upload log bench-all port clean firmware flash-firmware test

setup: venv
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install $(CORE)
	$(ARDUINO_CLI) lib update-index
	$(ARDUINO_CLI) lib install $(LIBS)
	$(ARDUINO_CLI) core list
	$(ARDUINO_CLI) lib list

venv: $(PY)
$(PY):
	python3 -m venv .venv
	.venv/bin/pip install --quiet pyserial==3.5

build:
	$(ARDUINO_CLI) compile --fqbn $(FQBN) --build-path $(BUILD_DIR) --warnings default $(SKETCH)

upload:
	@test -n "$(PORT)" || (echo "no /dev/cu.usbmodem* port: plug the CoreMatrix in" && false)
	$(ARDUINO_CLI) upload --fqbn $(FQBN) --input-dir $(BUILD_DIR) -p $(PORT) $(SKETCH)

flash: build upload

log: venv
	@test -n "$(PORT)" || (echo "no /dev/cu.usbmodem* port: plug the CoreMatrix in" && false)
	$(PY) -I tools/serlog.py $(PORT) $(SECS) $(OUT) $(SEND)

bench-all:
	@for s in bench/check*/; do $(MAKE) --no-print-directory build SKETCH=$${s%/} || exit 1; done

firmware:
	$(MAKE) --no-print-directory build SKETCH=$(FW)

flash-firmware:
	$(MAKE) --no-print-directory flash SKETCH=$(FW)

test:
	@mkdir -p .build
	c++ -std=c++17 -Wall -Wextra -O1 -o .build/host_test firmware/test/host_test.cpp
	.build/host_test

port:
	@echo "PORT=$(PORT)"
	@ioreg -p IOUSB -l -w0 | grep -E '"USB Product Name"|"idVendor"|"idProduct"' | grep -B1 -A1 'JTAG' || true

clean:
	rm -rf .build
