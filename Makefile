LDFLAGS=-L/usr/local/lib -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_features2d -lopencv_calib3d

CXXFLAGS=-Wall -I .. -I . -I apple -O3

TSRC=cpu.cpp util/testharness.cpp

OPENCVOBJS=cpu.o opencv/dummy-speaker.o opencv/opencv-display.o opencv/opencv-keyboard.o opencv/opencv-paddles.o opencv/opencv-filemanager.o apple/appledisplay.o apple/applekeyboard.o apple/applemmu.o apple/applevm.o apple/diskii.o apple/nibutil.o apple/slot.o RingBuffer.o globals.o opencv/aiie.o

ROMS=apple/applemmu-rom.h apple/diskii-rom.h

all: opencv

opencv: roms $(OPENCVOBJS)
	g++ $(LDFLAGS) -o aiie-opencv $(OPENCVOBJS)

clean:
	rm -f *.o *~ */*.o */*~ testharness.basic testharness.verbose testharness.extended aiie-opencv apple/diskii-rom.h apple/applemmu-rom.h

test: $(TSRC)
	g++ $(CXXFLAGS) -DBASICTEST $(TSRC) -o testharness.basic
	g++ $(CXXFLAGS) -DVERBOSETEST $(TSRC) -o testharness.verbose
	g++ $(CXXFLAGS) -DEXTENDEDTEST $(TSRC) -o testharness.extended

roms: apple2e.rom disk.rom
	./util/genrom.pl apple2e.rom disk.rom

apple/applemmu-rom.h: roms

apple/diskii-rom.h: roms

