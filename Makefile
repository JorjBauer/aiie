LDFLAGS=-L/usr/local/lib 

SDLLIBS=-lSDL2

CXXFLAGS=-Wall -I .. -I . -I apple -I sdl -I/usr/local/include/SDL2 -O3 -g

TSRC=cpu.cpp util/testharness.cpp

COMMONOBJS=cpu.o apple/appledisplay.o apple/applekeyboard.o apple/applemmu.o apple/applevm.o apple/diskii.o apple/nibutil.o RingBuffer.o globals.o apple/parallelcard.o apple/fx80.o apple/sy6522.o apple/ay8910.o lcg.o

SDLOBJS=sdl/sdl-speaker.o sdl/sdl-display.o sdl/sdl-keyboard.o sdl/sdl-paddles.o sdl/sdl-filemanager.o sdl/aiie.o sdl/sdl-printer.o sdl/sdl-clock.o

ROMS=apple/applemmu-rom.h apple/diskii-rom.h apple/parallel-rom.h

all: sdl

sdl: roms $(COMMONOBJS) $(SDLOBJS)
	g++ $(LDFLAGS) $(SDLLIBS) -o aiie-sdl $(COMMONOBJS) $(SDLOBJS)

clean:
	rm -f *.o *~ */*.o */*~ testharness.basic testharness.verbose testharness.extended apple/diskii-rom.h apple/applemmu-rom.h apple/parallel-rom.h aiie-sdl

test: $(TSRC)
	g++ $(CXXFLAGS) -DBASICTEST $(TSRC) -o testharness.basic
	g++ $(CXXFLAGS) -DVERBOSETEST $(TSRC) -o testharness.verbose
	g++ $(CXXFLAGS) -DEXTENDEDTEST $(TSRC) -o testharness.extended

roms: apple2e.rom disk.rom parallel.rom
	./util/genrom.pl apple2e.rom disk.rom parallel.rom

apple/applemmu-rom.h: roms

apple/diskii-rom.h: roms

apple/parallel-rom.h: roms
