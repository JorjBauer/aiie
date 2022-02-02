# Aiie!

Aiie! is an Apple //e emulator, written ground-up for the Teensy
4.1 (originally for the Teensy 3.6).

The name comes from a game I used to play on the Apple //e back
around 1986 - Ali Baba and the Forty Thieves, published by Quality
Software in 1981.

[http://crpgaddict.blogspot.com/2013/07/game-103-ali-baba-and-forty-thieves-1981.html](http://crpgaddict.blogspot.com/2013/07/game-103-ali-baba-and-forty-thieves-1981.html)

When characters in the game did damage to each other, they exclaimed
something like "HAH! JUST A SCRATCH!" or "AAARGH!" or "OH MA, I THINK
ITS MY TIME" [sic]. One of these exclamations was "AIIYEEEEE!!"

## Build log:

  [https://hackaday.io/project/19925-aiie-an-embedded-apple-e-emulator](https://hackaday.io/project/19925-aiie-an-embedded-apple-e-emulator)

# Getting the ROMs

As with many emulators, you have to go get the ROMs yourself. I've got
the ROMs that I dumped out of my Apple //e. You can probably get yours
a lot easier.

There are four files that you'll need:

* apple2e.rom  -- a 32k dump of the entire Apple //e ROM
* disk.rom     -- a 256 byte dump of the DiskII controller ROM (16-sector P5)
* parallel.rom -- a 256 byte dump of the Apple Parallel Card
* HDDRVR.BIN   -- a 256 byte hard drive driver from AppleWin
     (https://github.com/AppleWin/AppleWin/blob/master/firmware/HDD/HDDRVR.BIN)

The MD5 sums of those files are:

* 003a780b461c96ae3e72861ed0f4d3d9 apple2e.rom
* 2020aa1413ff77fe29353f3ee72dc295 disk.rom
* 5902996f16dc78fc013f6e1db14805b3 parallel.rom
  (this is the "parallel mode" ROM, not the "Centronics mode" ROM,
   and is availble on the Asimov mirror.)
* e91f379957d87aa0af0c7255f6ee6ba0 HDDRVR.BIN (from 2016) although 
  e0a40e9166af27b16f60beb83c9233f0 (from 2021) seems to be fine.

From those, the appropriate headers will be automatically generated by
"make roms" (or any other target that relies on the ROMs).

# Building (for the Teensy)

The directory 'teensy' contains 'teensy.ino' - the Arduino development
environment project file. You'll need to open that up and compile from
within.

However.

I built this on a Mac, and I used a lot of symlinks because of
limitations in the Arduino IDE. There's no reason that shouldn't work
under Linux, but I have absolutely no idea what Windows will make of
it. I would expect trouble. No, I won't accept pull requests that
remove the symlinks and replace them with the bare files. Sorry.

Also, you'll have to build the ROM headers (above) with 'make roms'
before you can build the Teensy .ino file.

If anyone knows how to make the Arduino development environment do any
form of scripting that could be used to generate those headers, I'd
gladly adopt that instead of forcing folks to run the Perl script via
Makefile. And if you have a better way of dealing with subfolders of
code, with the Teensy-specific code segregated as it is, I'm all ears...

I compile this with optimization set to "Faster" for the Teensy 4.1 at
600MHz. I've been successful underclocking it to increase the battery
life. YMMV.

## Environment and Libraries

I built this with arduino 1.8.13 and TeensyDuino 1.54b5.

      https://www.pjrc.com/teensy/td_download.html

These libraries I'm using right from Teensy's environment: TimerOne;
SPI; EEPROM; Time; Keypad; SdFat (previously called "SdFat-beta" but
renamed in TeensyDuino 1.54).

You'll also need the ILI9341_t3n library from

       https://github.com/KurtE/ILI9341_t3n/

As of this writing, the master branch does not work for Aiie; but the
branch "dma_new_fix" is fine. I'd recommend checking out that branch
if it exists.

# Running (on the Teensy)

The reset/menu button brings up a BIOS menu with options like:

    Resume
    Reset
    Cold Reboot
    Drop to Monitor
    Debug: off
    Suspend/Restore VM

    Display: RGB
    CPU Speed: Normal (1.023 MHz)
    Paddle X/Y inverted
    Configure paddles
    Volume +/-
    
    Insert/Eject Disk 1
    Insert/Eject Disk 2
    Insert/Eject HD 1
    Insert/Eject HD 2

## Reset

This is the same as control-reset on the actual hardware. If you
want to execute the Apple //e self-test, then hold down the two
joystick buttons; hit the reset/menu key; and select "Reset".

## Cold Reboot

This resets much of the hardware to a default state and forces a
reboot. It ejects any inserted disks. (You can get the self-test using
this, too.)

## Drop to Monitor

"Drop to Monitor" tries fairly hard to get you back to a monitor
prompt. Useful for debugging, probably not for much else.

## Display

"Display" has four values, and they're only really implemented for
text and hi-res modes (not for lo-res modes). To describe them, I have
to talk about the details of the Apple II display system.

In hires modes, the Apple II can only display certain colors in
certain horizontal pixel columns. Because of how the composite video
out works, the color "carries over" from one pixel to its neighbor;
multiple pixels turned on in a row makes them all white. Which means
that, if you're trying to display a picture in hires mode, you get
color artifacts on the edges of white areas.

The Apple Color Composite Monitor had a button on it that turned on
"Monochrome" mode, with the finer resolution necessary to display the
pixels without the color cast. So its two display modes would be the
ones I call "NTSC-like" and "Black and White."

There are two other video modes. The "RGB" mode (the default, because
it's my preference) shows the color pixels as they're actually drawn
in to memory. That means there can't be a solid field of, for example,
orange; there can only be vertical stripes of orange with black
between them.

The last mode is "Monochrome" which looks like the original "Monitor
II", a black-and-green display.

## Debug

This has several settings:

	off
	Show FPS
	Show mem free
	Show paddles
	Show PC (program counter)
	Show cycles (CPU run cycle count)
	Show battery (raw data and percentage)
	Show time (clock time)
	Show disk (selected drive / head position)

... these are all fairly self-explanatory.

## Insert/Eject Disk1/2 HD1/2

Fairly self-explanatory. Disks may be .dsk, .po, .nib, or .woz images
(although .nib images aren't very heavily tested, particularly for
write support). Hard drives are raw 32MB files, whose filenames must
end in .img.

## Suspend and Restore

The Teensy can be fully suspended and restored - including what disks
are inserted. It's a full VM hibernation. It currently writes to a
file named "suspend.vm" in the root of the MicroSD card. (I would like
to be able to select from multiple suspend/restore files
eventually. It wouldn't be terribly hard; it's just that the BIOS
interface is very limited.)

# Building (on a Mac)

While this isn't the purpose of the emulator, it is functional, and is
my first test target for most of the work. With MacOS 10.11.6 and
Homebrew, you can build and run it like this:

```
<pre>
  $ make sdl
  $ ./aiie-sdl /path/to/disk.dsk
</pre>
```

As the name implies, this requires that SDL is installed and in
/usr/local/lib. I've done that with Homebrew like this

```
<pre>
  $ brew install sdl2
</pre>
```

When running, F10 enters the BIOS.

# Building (on Linux)

I've been experimenting with Aiie running under a handmade OS on a Raspberry Pi Zero W; the hardware is decent, and cheap. I just don't want Linux in the way. So I built JOSS (see [https://hackaday.io/project/19925-aiie-an-embedded-apple-e-emulator/log/87286-entry-18-pi-zero-w-and-joss](my Hackaday page about JOSS)). 

Well, performance under JOSS is poor, so I built a Linux framebuffer wrapper for Aiie so that I can do performance testing on the Zero W, directly between JOSS and Linux. 

```
$ make linuxfb
$ ./linuxfb
```

# Mockingboard

Mockingboard support is slowly taking shape, based on the schematic in
the Apple II Documentation Project:

https://mirrors.apple2.org.za/Apple%20II%20Documentation%20Project/Interface%20Cards/Audio/Sweet%20Microsystems%20Mockingboard/Schematics/Mockingboard%20Schematic.gif

It was difficult to shoehorn this in to the Teensy 3.6, but with the
Teensy 4.1 it might be possible. More work required, though.

# VM

The virtual machine architecture is broken in half - the virtual and
physical pieces. There's the root VM object (vm.h), which ties
together the MMU, virtual keyboard, and virtual display.

Then there are the physical interfaces, which aren't as well
organized. They exist as globals in globals.cpp:

<pre>
	   FileManager *g_filemanager = NULL;
	   PhysicalDisplay *g_display = NULL;
	   PhysicalKeyboard *g_keyboard = NULL;
	   PhysicalSpeaker *g_speaker = NULL;
	   PhysicalPaddles *g_paddles = NULL;
</pre>

There are the two globals that point to the VM and the virtual CPU:

<pre>
	   Cpu *g_cpu = NULL;
	   VM *g_vm = NULL;
</pre>

And there are two global configuration values that probably belong in
some sort of Prefs class:

<pre>
	   int16_t g_volume;
	   uint8_t g_displayType;
</pre>


# CPU

The CPU is a full and complete 65C02. It supports all of the 65C02
documented and undocumented opcodes:

	   http://www.oxyron.de/html/opcodes02.html

The timing of the CPU is close to, but not quite, correct. It 
doesn't count cycles due to page boundary crossings during branch
instructions, for example. (See the "cycle count footnotes" in cpu.cpp.)

The CPU passes all of the the 6502 tests from here, including the undocumented ADC and SBC handling of Decimal mode and the overflow flag:

    https://github.com/Klaus2m5/6502_65C02_functional_tests


Doing a 

```
$ make test
```

will build the test harness and execute the three tests that encompass all others. (There are more tests in the **tests/** directory - only 3 of them are truly unique.) Two of the tests should emit

    All tests successful!

while the third says

    Test complete. Result: passed


# Caveats

This *requires* TeensyDuino 1.54 beta 5, which is the most recent
release as of this writing. There are two sets of functionality needed
here: first, SdFat with long file name support that works with the
Teensy 4; and second, raw USB keyboard scancode support.

Suspend/Restore is untested with the Teensy 4.1 hardware.

The Audio channel needs antialiasing and downsampling support for
cleaner audio. This wasn't a problem on the Teensy 3.6 because the
code was live toggling an actual speaker, just like the Apple did, in
real-time; but on the 4.1, we're using a digital audio interface so
we're experiencing real analog/digital conversion issues in some
situations (particularly code that exploits the physical hardware to
make really sophisticsated sounds).

CPU speed regulation isn't working at the moment; no matter what speed
you pick, you'll get normal full speed.

NIB disks are completely broken at the moment due to the Woz disk
format implementation. Internally, the NIB is converted to a WOZ and
apparently there's a bug somewhere.

The LinuxFB build is currently unmaintained, and definitely broken.

Many, but not all, copy-protected Woz disks work. There appears to be
a subtle timing bug in my disk driver code.

Disk write protection isn't implemented.

If you don't have an SD card inserted when you turn on Aiie, you can't
insert one and use it without power cycling. (The card driver is only
initialized on hardware startup.)

While I do have an ESP-01 wired in to the hardware, I don't have a
working driver written yet.


