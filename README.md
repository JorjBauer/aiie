Aiie!
=====

Aiie! is an Apple //e emulator, written ground-up for the Teensy
3.6.

The name comes from a game I used to play on the Apple //e back
around 1986 - Ali Baba and the Forty Thieves, published by Quality
Software in 1981.

http://crpgaddict.blogspot.com/2013/07/game-103-ali-baba-and-forty-thieves-1981.html

When characters in the game did damage to each other, they exclaimed
something like "HAH! JUST A SCRATCH!" or "AAARGH!" or "OH MA, I THINK
ITS MY TIME" [sic]. One of these exclamations was "AIIYEEEEE!!"

Build log:
----------

  https://hackaday.io/project/19925-aiie-an-embedded-apple-e-emulator

CPU
===

The CPU is a 65C02, not quite complete; it supports all of the 65C02
documented opcodes but not the undocumented ones here:

	   http://www.oxyron.de/html/opcodes02.html

The timing of the CPU is also not quite correct. It's close, but
doesn't count cycles due to page boundary crossings during branch
instructions. (See the "cycle count footnotes" in cpu.cpp.)

The CPU passes the 6502 functional test from here:

    https://github.com/Klaus2m5/6502_65C02_functional_tests

... which is included in binary form in the test harness (see the .h
files in util/ for notes).

testharness.basic should reach "test number 240", hang for a while,
and then exit.

testharness.verbose should show that it gets through 43 tests, test
240, and then loops repeatedly for a while (exiting at a somewhat
arbitrary point).

testharness.extended currently fails (hanging at 0x733) because I
haven't implemented the undocumented opcodes. It should get to address
0x24a8 and hang. Some day I'll finish implementing all of the
undocumented opcodes :)

