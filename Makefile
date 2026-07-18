LDFLAGS=-L/usr/local/lib -L/opt/homebrew/lib

SDLLIBS=-lSDL2 -lpthread
FBLIBS=-lpthread

CFLAGS=-Wall -I/usr/include/SDL2 -I .. -I . -I apple -I nix -I sdl -I/usr/local/include/SDL2 -I/opt/homebrew/include/SDL2 -g -O2 -DSUPPRESSREALTIME -DSTATICALLOC -DAIIE
CXXFLAGS=-Wall -I/usr/include/SDL2 -I .. -I . -I apple -I nix -I sdl -I/usr/local/include/SDL2 -I/opt/homebrew/include/SDL2 -g -O2 -DSUPPRESSREALTIME -DSTATICALLOC -DAIIE

TSRC=cpu.cpp util/testharness.cpp

COMMONSRCS=cpu.cpp apple/appledisplay.cpp apple/applekeyboard.cpp apple/applemmu.cpp apple/applevm.cpp apple/diskii.cpp apple/nibutil.cpp LRingBuffer.cpp globals.cpp apple/parallelcard.cpp apple/fx80.cpp lcg.cpp apple/hd32.cpp images.cpp apple/appleui.cpp vmram.cpp bios.cpp apple/noslotclock.cpp apple/woz.cpp apple/crc32.c apple/woz-serializer.cpp apple/mouse.c physicaldisplay.cpp wsola-speaker.cpp apple/mockingboard.cpp apple/uthernet2.cpp apple/usernet.cpp

COMMONOBJS=cpu.o apple/appledisplay.o apple/applekeyboard.o apple/applemmu.o apple/applevm.o apple/diskii.o apple/nibutil.o LRingBuffer.o globals.o apple/parallelcard.o apple/fx80.o lcg.o apple/hd32.o images.o apple/appleui.o vmram.o bios.o apple/noslotclock.o apple/woz.o apple/crc32.o apple/woz-serializer.o apple/mouse.o physicaldisplay.o wsola-speaker.o apple/mockingboard.o apple/uthernet2.o apple/usernet.o

FBSRCS=linuxfb/linux-speaker.cpp linuxfb/fb-display.cpp linuxfb/linux-keyboard.cpp linuxfb/fb-paddles.cpp nix/nix-filemanager.cpp linuxfb/aiie.cpp linuxfb/linux-printer.cpp nix/nix-clock.cpp nix/nix-prefs.cpp

FBOBJS=linuxfb/linux-speaker.o linuxfb/fb-display.o linuxfb/linux-keyboard.o linuxfb/fb-paddles.o nix/nix-filemanager.o linuxfb/aiie.o linuxfb/linux-printer.o nix/nix-clock.o nix/nix-prefs.o

SDLSRCS=sdl/sdl-speaker.cpp sdl/sdl-display.cpp sdl/sdl-keyboard.cpp sdl/sdl-paddles.cpp nix/nix-filemanager.cpp sdl/aiie.cpp sdl/sdl-printer.cpp nix/nix-clock.cpp nix/nix-prefs.cpp nix/debugger.cpp nix/disassembler.cpp sdl/sdl-mouse.cpp sdl/sdl-uthernet2.cpp sdl/usernet-bsd.cpp

SDLOBJS=sdl/sdl-speaker.o sdl/sdl-display.o sdl/sdl-keyboard.o sdl/sdl-paddles.o nix/nix-filemanager.o sdl/aiie.o sdl/sdl-printer.o nix/nix-clock.o nix/nix-prefs.o nix/debugger.o nix/disassembler.o sdl/sdl-mouse.o sdl/sdl-uthernet2.o sdl/usernet-bsd.o

ROMS=apple/applemmu-rom.h apple/diskii-rom.h apple/parallel-rom.h apple/hd32-rom.h apple/mouse-rom.h

# ---- Teensy 4.1 firmware build (via arduino-cli) ----
# The 'teensy' subdirectory is an Arduino sketch: teensy.ino plus symlinks to
# the shared sources and the Teensy-specific drivers. Historically it could
# only be built by opening it in the Arduino IDE; these targets drive
# arduino-cli instead so it can be built and flashed from the command line.
#
# Every setting below can be overridden on the command line, e.g.:
#   make teensy TEENSY_SPEED=600
#   make teensy-install PORT=/dev/cu.usbmodem12345
ARDUINO_CLI   ?= arduino-cli
TEENSY_BOARD  ?= teensy:avr:teensy41
# README builds at 528MHz ("Faster"); the 4.1 can go to 600. usb=serial
# because keyboard input arrives via USBHost, not the device USB type.
TEENSY_SPEED  ?= 528
TEENSY_OPT    ?= o2std
TEENSY_USB    ?= serial
TEENSY_FQBN   ?= $(TEENSY_BOARD):speed=$(TEENSY_SPEED),opt=$(TEENSY_OPT),usb=$(TEENSY_USB)
TEENSY_SKETCH ?= teensy
TEENSY_BUILD  ?= teensy/build
# Libraries pulled from the Arduino/Teensy environment (see README).
TEENSY_LIBS   ?= Time Bounce2 ILI9341_t3n
# Filename to write for the SD-card self-update ("Update firmware from SD").
TEENSY_SDIMAGE ?= AIIE.HEX
# Optional explicit upload port; if empty, the Teensy loader auto-detects a
# board in bootloader mode (you may need to press the button on the Teensy).
PORT          ?=

.PHONY: roms clean teensy teensy-libs teensy-upload teensy-install teensy-clean teensy-sdimage

all:
	@echo You want \'make sdl\', \'make linuxfb\', or \'make teensy\'.

sdl: roms $(COMMONOBJS) $(SDLOBJS)
	g++ $(LDFLAGS) $(SDLLIBS) -o aiie-sdl $(COMMONOBJS) $(SDLOBJS)

linuxfb: roms $(COMMONOBJS) $(FBOBJS)
	g++ $(LDFLAGS) $(FBLIBS) -o aiie-fb $(COMMONOBJS) $(FBOBJS)

# Compile the Teensy firmware. ROM headers (built by 'roms') are symlinked
# into the sketch folder, so they must exist before arduino-cli runs.
teensy: roms
	$(ARDUINO_CLI) compile --fqbn $(TEENSY_FQBN) --output-dir $(TEENSY_BUILD) $(TEENSY_SKETCH)

# One-time helper to install the libraries the sketch depends on.
teensy-libs:
	$(ARDUINO_CLI) lib install $(TEENSY_LIBS)

# Build and flash the board in one arduino-cli invocation. 'compile -u' is
# used (rather than a separate 'upload') because the Teensy loader resolves
# the firmware as {build.path}/{project}.hex from the compile step; a
# standalone 'upload --input-dir' hands it a path it can't find. The compile
# reuses arduino-cli's build cache, so this is fast after 'make teensy'.
# If the board isn't auto-detected, pass PORT=/dev/cu.usbmodemNNNN.
# teensy-install is an alias for teensy-upload.
teensy-upload teensy-install: roms
	$(ARDUINO_CLI) compile --fqbn $(TEENSY_FQBN) -u $(if $(PORT),-p $(PORT),) $(TEENSY_SKETCH)

teensy-clean:
	rm -rf $(TEENSY_BUILD)

# Build, then copy the firmware image to $(TEENSY_SDIMAGE) for SD-card
# self-update: drop that file in the root of the Teensy's MicroSD card and
# pick "Update firmware from SD" in the BIOS VM menu.
teensy-sdimage: teensy
	cp $(TEENSY_BUILD)/teensy.ino.hex $(TEENSY_SDIMAGE)
	@echo "Wrote $(TEENSY_SDIMAGE) -- copy it to the root of the Teensy SD card."

test: $(TSRC)
	g++ $(CXXFLAGS) -DEXIT_ON_ILLEGAL -DVERBOSE_CPU_ERRORS -DTESTHARNESS $(TSRC) -o testharness
	./testharness -f tests/6502_functional_test_verbose.bin -s 0x400 && \
	./testharness -f tests/65C02_extended_opcodes_test.bin -s 0x400 && \
	./testharness -f tests/65c02-all.bin -s 0x200

# Characterize DiskII's LSS read path and exercise the write path by
# round-tripping bytes through the LSS. Build with AIIE off so woz.cpp
# uses POSIX file I/O directly instead of the filemanager wrapper.
DISKIITEST_SRCS = tests/test-diskii.cpp \
                  apple/diskii.cpp apple/woz.cpp apple/woz-serializer.cpp \
                  apple/nibutil.cpp apple/crc32.c \
                  LRingBuffer.cpp vmram.cpp cpu.cpp lcg.cpp
DISKIITEST_FLAGS = -Wall -g -I .. -I . -I apple -I nix -I sdl \
                   -DSUPPRESSREALTIME -DSTATICALLOC

test-diskii: roms $(DISKIITEST_SRCS) tests/test-diskii.cpp
	g++ $(DISKIITEST_FLAGS) $(DISKIITEST_SRCS) -o tests/test-diskii
	./tests/test-diskii

roms: apple2e.rom disk.rom parallel.rom HDDRVR.BIN mouse.rom
	./util/genrom.pl apple2e.rom disk.rom parallel.rom HDDRVR.BIN mouse.rom

apple/applemmu-rom.h: roms

apple/diskii-rom.h: roms

apple/parallel-rom.h: roms

apple/mouse-rom.h: roms

clean:
	rm -f *.o *~ */*.o */*~ testharness.basic testharness.verbose testharness.extended testharness apple/diskii-rom.h apple/applemmu-rom.h apple/parallel-rom.h aiie-sdl *.d */*.d
	rm -rf $(TEENSY_BUILD)

# Automatic dependency handling
-include *.d
-include apple/*.d
-include nix/*.d
-include sdl/*.d

%.o: %.cpp
	g++ $(CXXFLAGS) -MMD -MP -c $< -o $@
