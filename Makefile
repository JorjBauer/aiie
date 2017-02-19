LDFLAGS=-L/usr/local/lib
CXXFLAGS=-Wall -I .. -I . -O3

TSRC=cpu.cpp util/testharness.cpp

all:
	@echo There is no 'all' target. Yet.

clean:
	rm -f *.o *~ */*.o */*~ testharness.basic testharness.verbose testharness.extended

test: $(TSRC)
	g++ $(CXXFLAGS) -DBASICTEST $(TSRC) -o testharness.basic
	g++ $(CXXFLAGS) -DVERBOSETEST $(TSRC) -o testharness.verbose
	g++ $(CXXFLAGS) -DEXTENDEDTEST $(TSRC) -o testharness.extended

