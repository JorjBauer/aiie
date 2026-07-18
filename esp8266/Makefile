# Makefile for the aiie ESP-01 network co-processor firmware.
# Driven by arduino-cli + the esp8266:esp8266 core.
#
#   make setup                 # one-time: add board URL + install the core
#   make                       # compile (default target)
#   make upload PORT=/dev/cu.usbserial-XXXX
#   make monitor PORT=/dev/cu.usbserial-XXXX
#   make clean
#
# Flashing an ESP-01 needs a USB-serial (3.3V!) adapter with GPIO0 held LOW
# during reset to enter the bootloader.  On aiie r9 GPIO0 is only pulled up
# (not Teensy-driven), so program the module on the bench, not in-circuit.

ARDUINO_CLI ?= arduino-cli

# Base board. ESP-01 (blue) = 512K flash; ESP-01S (black) = 1M. Adjust eesz
# (run `make info` for the full list of valid values):
#   ESP-01  (512K): BOARD_OPTS=eesz=512K64,baud=115200,xtal=80
#   ESP-01S (1M)  : BOARD_OPTS=eesz=1M,baud=115200,xtal=80     (default)
FQBN       ?= esp8266:esp8266:generic
BOARD_OPTS ?= eesz=1M,baud=115200,xtal=80
FQBN_FULL  := $(FQBN):$(BOARD_OPTS)

# Serial port of your USB-serial adapter (NOT the Teensy).
PORT        ?= /dev/cu.usbserial-0001
LINK_BAUD   ?= 115200      # runtime UART speed to the Teensy (see esp8266.ino)

BOARD_URL   := https://arduino.esp8266.com/stable/package_esp8266com_index.json
BUILD_DIR   := build

# Pass LINK_BAUD through as a compile-time -D. compiler.cpp.extra_flags is the
# append hook (it does not clobber the core's own build.extra_flags).
CLI_FLAGS   := --build-property compiler.cpp.extra_flags=-DLINK_BAUD=$(LINK_BAUD)

.PHONY: all build compile setup upload monitor clean info

all: build

build compile:
	$(ARDUINO_CLI) compile --fqbn $(FQBN_FULL) --build-path $(BUILD_DIR) $(CLI_FLAGS) .

setup:
	$(ARDUINO_CLI) config add board_manager.additional_urls $(BOARD_URL) || true
	$(ARDUINO_CLI) core update-index
	$(ARDUINO_CLI) core install esp8266:esp8266

upload:
	$(ARDUINO_CLI) upload -p $(PORT) --fqbn $(FQBN_FULL) --input-dir $(BUILD_DIR) .

monitor:
	$(ARDUINO_CLI) monitor -p $(PORT) -c baudrate=$(LINK_BAUD)

info:
	$(ARDUINO_CLI) board details --fqbn $(FQBN)

clean:
	rm -rf $(BUILD_DIR)
