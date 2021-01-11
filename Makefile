LDFLAGS=-L/usr/local/lib 

SDLLIBS=-lSDL2 -lpthread
FBLIBS=-lpthread

CFLAGS=-Wall -I/usr/include/SDL2 -I .. -I . -I apple -I nix -I sdl -I/usr/local/include/SDL2 -g -DSUPPRESSREALTIME -DSTATICALLOC
CXXFLAGS=-Wall -I/usr/include/SDL2 -I .. -I . -I apple -I nix -I sdl -I/usr/local/include/SDL2 -g -DSUPPRESSREALTIME -DSTATICALLOC

TSRC=cpu.cpp util/testharness.cpp

COMMONSRCS=cpu.cpp apple/appledisplay.cpp apple/applekeyboard.cpp apple/applemmu.cpp apple/applevm.cpp apple/diskii.cpp apple/nibutil.cpp LRingBuffer.cpp globals.cpp apple/parallelcard.cpp apple/fx80.cpp lcg.cpp apple/hd32.cpp images.cpp apple/appleui.cpp vmram.cpp bios.cpp apple/noslotclock.cpp apple/woz.cpp apple/crc32.c apple/woz-serializer.cpp apple/mouse.c

COMMONOBJS=cpu.o apple/appledisplay.o apple/applekeyboard.o apple/applemmu.o apple/applevm.o apple/diskii.o apple/nibutil.o LRingBuffer.o globals.o apple/parallelcard.o apple/fx80.o lcg.o apple/hd32.o images.o apple/appleui.o vmram.o bios.o apple/noslotclock.o apple/woz.o apple/crc32.o apple/woz-serializer.o apple/mouse.o

FBSRCS=linuxfb/linux-speaker.cpp linuxfb/fb-display.cpp linuxfb/linux-keyboard.cpp linuxfb/fb-paddles.cpp nix/nix-filemanager.cpp linuxfb/aiie.cpp linuxfb/linux-printer.cpp nix/nix-clock.cpp nix/nix-prefs.cpp

FBOBJS=linuxfb/linux-speaker.o linuxfb/fb-display.o linuxfb/linux-keyboard.o linuxfb/fb-paddles.o nix/nix-filemanager.o linuxfb/aiie.o linuxfb/linux-printer.o nix/nix-clock.o nix/nix-prefs.o

SDLSRCS=sdl/sdl-speaker.cpp sdl/sdl-display.cpp sdl/sdl-keyboard.cpp sdl/sdl-paddles.cpp nix/nix-filemanager.cpp sdl/aiie.cpp sdl/sdl-printer.cpp nix/nix-clock.cpp nix/nix-prefs.cpp nix/debugger.cpp nix/disassembler.cpp sdl/sdl-mouse.cpp

SDLOBJS=sdl/sdl-speaker.o sdl/sdl-display.o sdl/sdl-keyboard.o sdl/sdl-paddles.o nix/nix-filemanager.o sdl/aiie.o sdl/sdl-printer.o nix/nix-clock.o nix/nix-prefs.o nix/debugger.o nix/disassembler.o sdl/sdl-mouse.o

ROMS=apple/applemmu-rom.h apple/diskii-rom.h apple/parallel-rom.h apple/hd32-rom.h 

.PHONY: roms clean

all: 
	@echo You want \'make sdl\' or \'make linuxfb\'.

sdl: roms $(COMMONOBJS) $(SDLOBJS)
	g++ $(LDFLAGS) $(SDLLIBS) -o aiie-sdl $(COMMONOBJS) $(SDLOBJS)

linuxfb: roms $(COMMONOBJS) $(FBOBJS)
	g++ $(LDFLAGS) $(FBLIBS) -o aiie-fb $(COMMONOBJS) $(FBOBJS)

test: $(TSRC)
	g++ $(CXXFLAGS) -DEXIT_ON_ILLEGAL -DVERBOSE_CPU_ERRORS -DTESTHARNESS $(TSRC) -o testharness
	./testharness -f tests/6502_functional_test_verbose.bin -s 0x400 && \
	./testharness -f tests/65C02_extended_opcodes_test.bin -s 0x400 && \
	./testharness -f tests/65c02-all.bin -s 0x200

roms: apple2e.rom disk.rom parallel.rom HDDRVR.BIN
	./util/genrom.pl apple2e.rom disk.rom parallel.rom HDDRVR.BIN 

apple/applemmu-rom.h: roms

apple/diskii-rom.h: roms

apple/parallel-rom.h: roms

clean:
	rm -f *.o *~ */*.o */*~ testharness.basic testharness.verbose testharness.extended testharness apple/diskii-rom.h apple/applemmu-rom.h apple/parallel-rom.h aiie-sdl *.d */*.d

# Automatic dependency handling
-include *.d
-include apple/*.d
-include nix/*.d
-include sdl/*.d

%.o: %.cpp
	g++ $(CXXFLAGS) -MMD -MP -c $< -o $@
