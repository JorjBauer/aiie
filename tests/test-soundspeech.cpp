// test-soundspeech: the Sound/Speech I card (apple/mockingboard.cpp with
// soundSpeech = true), driven through its slot space the way speech software
// drives it.
//
// The protocol under test is the one speech software uses: DDRB = $FF,
// PCR = $B0 (CB2 pulses on an ORB write, CB1 flags on a rising edge), IER =
// $90 (CB1), then each phoneme is an ORB write and the next one goes out when
// the CB1 interrupt says the SC-01's A/R line went high. Every check
// requires the positive result: the flag set on the cycle it should be, the
// IRQ line held, the AY register still holding what we put there.
//
// Most of this covers the bus side: the handshake,
// the timing, the IRQ, and the layout of a real card (the $Cn00 6522 for the
// SC-01 alone, the AY on a $Cn80 6522 that drives NMI). The 6522 and AY
// checks pin down what a diagnostic disk expects; the details are in
// aiie-docs/mockingboard-timing.md.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "cpu.h"
#include "mockingboard.h"
#include "sc01params.h"
#include "globals.h"
#include "filemanager.h"

Cpu *g_cpu = NULL;
FileManager *g_filemanager = NULL;   // only Cpu::Serialize uses it

static Cpu cpu;
static int failures = 0;

static void check(bool ok, const char *what)
{
  if (!ok) {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

// 6522 registers, as offsets into $Cn00
#define ORB  0x00
#define ORA  0x01
#define DDRB 0x02
#define DDRA 0x03
#define T1CL 0x04
#define T1CH 0x05
#define PCR  0x0C
#define IFR  0x0D
#define IER  0x0E

static void ayWrite(Mockingboard *mb, uint8_t base, uint8_t reg, uint8_t val)
{
  mb->writeSlotRom(base + ORA, reg);
  mb->writeSlotRom(base + ORB, 0x07);   // latch address (BDIR|BC1, /RESET high)
  mb->writeSlotRom(base + ORB, 0x04);   // inactive
  mb->writeSlotRom(base + ORA, val);
  mb->writeSlotRom(base + ORB, 0x06);   // write
  mb->writeSlotRom(base + ORB, 0x04);
}

// Port A drives the register number out, then turns around to read the
// AY's answer
static uint8_t ayRead(Mockingboard *mb, uint8_t base, uint8_t reg)
{
  mb->writeSlotRom(base + DDRA, 0xFF);
  mb->writeSlotRom(base + ORA, reg);
  mb->writeSlotRom(base + ORB, 0x07);
  mb->writeSlotRom(base + ORB, 0x04);
  mb->writeSlotRom(base + DDRA, 0x00);
  mb->writeSlotRom(base + ORB, 0x05);   // read
  uint8_t v = mb->readSlotRom(base + ORA);
  mb->writeSlotRom(base + ORB, 0x04);
  mb->writeSlotRom(base + DDRA, 0xFF);
  return v;
}

static void runTo(Mockingboard *mb, int64_t cycle)
{
  g_cpu->cycles = cycle;
  mb->update(cycle);
}

static int64_t phoneCycles(uint8_t phone)
{
  // The datasheet chart's timing rule (aiie-docs/sc01-rom-analysis.md):
  // strobe to A/R high is 72 + 16 * (DUR*4+1) * 36 + 2 master clocks.
  return 72 + 16 * ((int64_t)sc01Phones[phone].dur * 4 + 1) * 36 + 2;
}

static void testLayout()
{
  Mockingboard ss(true);
  ss.Reset();
  runTo(&ss, 1000);

  // A 6522 at $Cn00: timer 1 counts down
  ss.writeSlotRom(T1CL, 0xFF);
  ss.writeSlotRom(T1CH, 0x7F);
  runTo(&ss, 1100);
  uint8_t hi = ss.readSlotRom(T1CH);
  check(hi == 0x7F || hi == 0x7E, "$Cn05 is a running timer 1 on the Sound/Speech I");
  uint8_t lo = ss.readSlotRom(T1CL);
  check(lo != 0xFF, "timer 1 low byte has counted down");

  // A second 6522 at $Cn80
  ss.writeSlotRom(0x80 + DDRA, 0xFF);
  check(ss.readSlotRom(0x80 + DDRA) == 0xFF, "$Cn83 is a second 6522's DDRA");

  // The one AY works through the $Cn80 6522
  ss.writeSlotRom(0x80 + DDRB, 0xFF);
  ayWrite(&ss, 0x80, 8, 0x0C);
  ss.writeSlotRom(0x80 + DDRA, 0x00);
  check(ayRead(&ss, 0x80, 8) == 0x0C, "AY register 8 round-trips through $Cn80");

  // and there's no AY behind the $Cn00 6522
  ss.writeSlotRom(DDRB, 0xFF);
  ss.writeSlotRom(DDRA, 0xFF);
  ayWrite(&ss, 0x00, 8, 0x05);
  ss.writeSlotRom(DDRA, 0x00);
  check(ayRead(&ss, 0x00, 8) != 0x05, "no AY answers through $Cn00");
  ss.writeSlotRom(0x80 + DDRA, 0x00);
  check(ayRead(&ss, 0x80, 8) == 0x0C, "a write through $Cn00 didn't reach the AY");

  // A Mockingboard A still has both
  Mockingboard mb;
  mb.Reset();
  runTo(&mb, 1000);
  mb.writeSlotRom(0x80 + DDRA, 0xFF);
  check(mb.readSlotRom(0x80 + DDRA) == 0xFF, "Mockingboard A still has a 6522 at $Cn80");
}

static void testHandshake()
{
  Mockingboard ss(true);
  ss.Reset();
  g_cpu->Reset();
  runTo(&ss, 1000);

  // Music first: AY volume A = $0F
  ss.writeSlotRom(0x80 + DDRB, 0xFF);
  ss.writeSlotRom(0x80 + DDRA, 0xFF);
  ayWrite(&ss, 0x80, 8, 0x0F);

  // Speech setup
  ss.writeSlotRom(PCR, 0xB0);
  ss.writeSlotRom(IER, 0x90);
  check(!g_cpu->irqPending, "no IRQ before the first phoneme");

  // EH3 ($00) has bit 2 clear, which would be /RESET to an AY
  ss.writeSlotRom(DDRB, 0xFF);
  int64_t t0 = 2000;
  runTo(&ss, t0);
  ss.writeSlotRom(ORB, 0x00);
  int64_t n = phoneCycles(0x00);
  check(n > 10000, "EH3 lasts more than 10ms of CPU time");

  runTo(&ss, t0 + n - 1);
  check((ss.readSlotRom(IFR) & 0x10) == 0, "CB1 still clear one cycle before A/R rises");
  check(!g_cpu->irqPending, "no IRQ one cycle before A/R rises");

  runTo(&ss, t0 + n);
  check((ss.readSlotRom(IFR) & 0x90) == 0x90, "CB1 and IRQ set in IFR when A/R rises");
  check(g_cpu->irqPending, "the CPU's IRQ line is held when A/R rises");

  // The next phoneme clears CB1 and releases the line
  runTo(&ss, t0 + n + 50);
  ss.writeSlotRom(ORB, 0x3F);   // STOP
  check((ss.readSlotRom(IFR) & 0x10) == 0, "an ORB write clears CB1");
  check(!g_cpu->irqPending, "an ORB write releases the IRQ line");

  int64_t t1 = t0 + n + 50;
  runTo(&ss, t1 + phoneCycles(0x3F));
  check((ss.readSlotRom(IFR) & 0x10) == 0x10, "STOP's A/R rise sets CB1 at its own length");

  // The phoneme writes never reached the AY
  ss.writeSlotRom(0x80 + DDRA, 0x00);
  check(ayRead(&ss, 0x80, 8) == 0x0F, "phoneme writes didn't reset or touch the AY");
}

static void testNoInterruptWhenMasked()
{
  Mockingboard ss(true);
  ss.Reset();
  g_cpu->Reset();
  runTo(&ss, 1000);
  ss.writeSlotRom(DDRB, 0xFF);
  ss.writeSlotRom(PCR, 0xB0);
  ss.writeSlotRom(IER, 0x10);   // clear the CB1 enable
  runTo(&ss, 2000);
  ss.writeSlotRom(ORB, 0x03);   // PA0
  runTo(&ss, 2000 + phoneCycles(0x03));
  uint8_t ifr = ss.readSlotRom(IFR);
  check((ifr & 0x10) == 0x10, "CB1 flags even with its interrupt masked (polling works)");
  check((ifr & 0x80) == 0, "no IRQ bit in IFR when CB1 is masked");
  check(!g_cpu->irqPending, "no IRQ when CB1 is masked");
}

static void testFallingEdge()
{
  Mockingboard ss(true);
  ss.Reset();
  runTo(&ss, 1000);
  ss.writeSlotRom(DDRB, 0xFF);
  ss.writeSlotRom(PCR, 0xA0);   // CB2 pulse, CB1 on a falling edge
  runTo(&ss, 2000);
  ss.writeSlotRom(ORB, 0x03);
  check((ss.readSlotRom(IFR) & 0x10) == 0x10, "with PCR $A0, CB1 flags as A/R falls on the strobe");
}

static void testWiredOr()
{
  Mockingboard ss(true);
  Mockingboard mb;
  ss.Reset();
  mb.Reset();
  g_cpu->Reset();
  runTo(&ss, 1000);
  runTo(&mb, 1000);

  // Another card (the mouse) holds the line
  g_cpu->assertIrq();
  // The Sound/Speech I raises and then drops its own interrupt
  ss.writeSlotRom(DDRB, 0xFF);
  ss.writeSlotRom(PCR, 0xB0);
  ss.writeSlotRom(IER, 0x90);
  runTo(&ss, 2000);
  ss.writeSlotRom(ORB, 0x03);
  runTo(&ss, 2000 + phoneCycles(0x03));
  ss.readSlotRom(IFR);
  ss.writeSlotRom(ORB, 0x3F);   // clears CB1, so the card lets go
  check(g_cpu->irqPending, "the card letting go doesn't drop the mouse's IRQ");

  // A Mockingboard A timer interrupt, released, doesn't drop it either
  mb.writeSlotRom(IER, 0xC0);
  mb.writeSlotRom(T1CL, 0x10);
  mb.writeSlotRom(T1CH, 0x00);
  runTo(&mb, 1100);
  mb.writeSlotRom(IFR, 0x40);
  check(g_cpu->irqPending, "the Mockingboard A letting go doesn't drop the mouse's IRQ");

  g_cpu->deassertIrq();
  check(!g_cpu->irqPending, "the line is free once every source lets go");
}

// The $Cn80 6522 drives NMI, which only fires when the line falls
static void testNmi()
{
  Mockingboard ss(true);
  ss.Reset();
  g_cpu->Reset();
  runTo(&ss, 1000);
  check(!g_cpu->nmiPending, "no NMI before the timer runs out");

  ss.writeSlotRom(0x80 + IER, 0xC0);   // timer 1
  ss.writeSlotRom(0x80 + T1CL, 0x10);
  ss.writeSlotRom(0x80 + T1CH, 0x00);
  runTo(&ss, 1100);
  check(g_cpu->nmiPending, "the $Cn80 timer interrupt is an NMI");
  check(!g_cpu->irqPending, "and not an IRQ");

  // We hold the line low until the flag clears, so no second NMI
  g_cpu->nmiPending = false;
  ss.readSlotRom(0x80 + IFR);
  runTo(&ss, 1110);
  check(!g_cpu->nmiPending, "a held NMI line doesn't fire twice");

  // Clear the flag, run out again: a new falling edge
  ss.writeSlotRom(0x80 + IFR, 0x40);
  ss.writeSlotRom(0x80 + T1CH, 0x00);
  runTo(&ss, 1400);
  check(g_cpu->nmiPending, "the next timeout falls again and fires again");

  // The Mockingboard A's $Cn80 6522 is still an IRQ
  Mockingboard mb;
  mb.Reset();
  g_cpu->Reset();
  runTo(&mb, 1000);
  mb.writeSlotRom(0x80 + IER, 0xC0);
  mb.writeSlotRom(0x80 + T1CL, 0x10);
  mb.writeSlotRom(0x80 + T1CH, 0x00);
  runTo(&mb, 1100);
  check(g_cpu->irqPending && !g_cpu->nmiPending, "Mockingboard A's $Cn80 timer is an IRQ");
}

// The 6522 timer rules, on the Mockingboard A's $Cn00 6522.
// Here every access lands on the cycle we set (outside an instruction,
// Cpu::busCycle() is just 'cycles').
static void testTimers()
{
  Mockingboard mb;
  mb.Reset();
  g_cpu->Reset();
  runTo(&mb, 1000);

  // T1C-H written on cycle w: the counter holds N on w+1 and counts down
  // from there: fourteen cycles after w+1, $FF reads $F1
  mb.writeSlotRom(T1CL, 0xFF);
  runTo(&mb, 2000);
  mb.writeSlotRom(T1CH, 0x00);
  g_cpu->cycles = 2001;
  check(mb.readSlotRom(T1CL) == 0xFF, "T1 holds N on the cycle after the write");
  g_cpu->cycles = 2015;
  check(mb.readSlotRom(T1CL) == 0xF1, "T1 reads N-14 fourteen cycles later");

  // The flag goes up when it rolls from 0 to $FFFF, N+2 cycles after the
  // write, and then T1 reloads from the latch (in one-shot mode too)
  Mockingboard m2;
  m2.Reset();
  g_cpu->Reset();
  runTo(&m2, 1000);
  m2.writeSlotRom(0x0B, 0x00);   // ACR: one-shot
  m2.writeSlotRom(T1CL, 0x10);
  m2.writeSlotRom(T1CH, 0x00);   // N = $10, written on cycle 1000
  runTo(&m2, 1000 + 0x10 + 1);
  check((m2.readSlotRom(IFR) & 0x40) == 0, "no T1 flag on the cycle T1 reads 0");
  runTo(&m2, 1000 + 0x10 + 2);
  check((m2.readSlotRom(IFR) & 0x40) == 0x40, "T1 flags N+2 cycles after the write");
  g_cpu->cycles = 1000 + 0x10 + 2;
  check(m2.readSlotRom(T1CH) == 0xFF, "T1 reads $FFFF as it flags");
  g_cpu->cycles = 1000 + 0x10 + 3;
  check(m2.readSlotRom(T1CH) == 0x00, "then reloads from the latch, even in one-shot mode");
  m2.writeSlotRom(IFR, 0x40);
  runTo(&m2, 1000 + 2 * (0x10 + 2) + 5);
  check((m2.readSlotRom(IFR) & 0x40) == 0, "one-shot: no second flag");

  // The CPU takes the IRQ only at a boundary 2 or more cycles after the
  // line fell, since it samples it on an instruction's next-to-last cycle
  Mockingboard m3;
  m3.Reset();
  g_cpu->Reset();
  runTo(&m3, 1000);
  m3.writeSlotRom(IER, 0xC0);
  m3.writeSlotRom(T1CL, 0x06);
  m3.writeSlotRom(T1CH, 0x00);   // flags on 1008
  runTo(&m3, 1008);
  check(g_cpu->irqPending && g_cpu->irqReadyCycle == 1010, "the IRQ is ready 2 cycles after the 6522 raises it");
}

// The AY-3-8913 on a Mockingboard A keeps only the bits each register uses
// and has no I/O ports; the Sound/Speech I's AY-3-8910 has them.
static void testAyVariants()
{
  Mockingboard mb;
  mb.Reset();
  g_cpu->Reset();
  runTo(&mb, 1000);
  mb.writeSlotRom(DDRB, 0xFF);
  mb.writeSlotRom(DDRA, 0xFF);
  mb.writeSlotRom(ORB, 0x04);    // out of reset
  ayWrite(&mb, 0x00, 1, 0x1F);
  check(ayRead(&mb, 0x00, 1) == 0x0F, "8913 R1 keeps 4 bits");
  ayWrite(&mb, 0x00, 7, 0xFF);
  check(ayRead(&mb, 0x00, 7) == 0x3F, "8913 R7 has no port direction bits");
  check(ayRead(&mb, 0x00, 14) == 0x0E, "8913 R14 leaves the latched address on the bus");

  // A reset leaves no register selected, so a write without a latch goes nowhere
  ayWrite(&mb, 0x00, 0, 0x00);
  mb.writeSlotRom(ORB, 0x00);    // /RESET
  mb.writeSlotRom(ORB, 0x04);
  mb.writeSlotRom(ORA, 0x42);
  mb.writeSlotRom(ORB, 0x06);    // write, with no address latched
  mb.writeSlotRom(ORB, 0x04);
  check(ayRead(&mb, 0x00, 0) == 0x00, "a write after reset selects no register");

  Mockingboard ss(true);
  ss.Reset();
  runTo(&ss, 1000);
  ss.writeSlotRom(0x80 + DDRB, 0xFF);
  ss.writeSlotRom(0x80 + DDRA, 0xFF);
  ss.writeSlotRom(0x80 + ORB, 0x04);
  ayWrite(&ss, 0x80, 7, 0xC0);   // both ports output
  ayWrite(&ss, 0x80, 14, 0x55);
  check(ayRead(&ss, 0x80, 7) == 0xC0, "8910 R7 keeps the port direction bits");
  check(ayRead(&ss, 0x80, 14) == 0x55, "8910 port A reads back as an output");
  ayWrite(&ss, 0x80, 7, 0x00);   // both ports input
  check(ayRead(&ss, 0x80, 14) == 0xFF, "8910 port A as an input reads pulled-up pins");
}

// Ctrl-Reset: the CPU's cycle clock restarts, and the 6522s see RES
static void testCtrlReset()
{
  Mockingboard ss(true);
  ss.Reset();
  g_cpu->Reset();
  runTo(&ss, 50000);
  ss.writeSlotRom(DDRB, 0xFF);
  ss.writeSlotRom(PCR, 0xB0);
  ss.writeSlotRom(IER, 0x90);
  ss.writeSlotRom(0x80 + 0x0B, 0x40);  // $Cn80 T1 free-running
  ss.writeSlotRom(0x80 + IER, 0xC0);
  ss.writeSlotRom(0x80 + T1CL, 0x34);
  ss.writeSlotRom(0x80 + T1CH, 0x12);
  runTo(&ss, 60000);
  ss.writeSlotRom(ORB, 0x00);          // EH3, the long one
  int64_t left = phoneCycles(0x00) - 100;
  runTo(&ss, 60100);

  g_cpu->Reset();                      // cycles go back to 6
  g_cpu->cycles = 1000;
  ss.busReset();
  check(ss.readSlotRom(0x0B) == 0 && ss.readSlotRom(PCR) == 0, "Ctrl-Reset clears ACR and PCR");
  check(ss.readSlotRom(DDRB) == 0, "Ctrl-Reset clears DDRB");
  check(ss.readSlotRom(IER) == 0x80, "Ctrl-Reset disables every interrupt");
  check(ss.readSlotRom(0x80 + 0x06) == 0x34 && ss.readSlotRom(0x80 + 0x07) == 0x12, "Ctrl-Reset keeps the T1 latch");
  check(!g_cpu->irqPending && !g_cpu->nmiPending, "nothing holds an interrupt line after Ctrl-Reset");
  runTo(&ss, 1000 + 0x1234 * 3);
  check((ss.readSlotRom(0x80 + IFR) & 0x60) == 0, "no timer flags after Ctrl-Reset");

  // The SC-01 isn't on the line: its phoneme finishes on time by the new clock
  ss.writeSlotRom(PCR, 0x10);          // CB1 on a rising edge again, to watch A/R
  runTo(&ss, 1000 + left - 50);
  check((ss.readSlotRom(IFR) & 0x10) == 0, "A/R still low just before the phoneme ends");
  runTo(&ss, 1000 + left + 50);
  check((ss.readSlotRom(IFR) & 0x10) == 0x10, "A/R rises when the phoneme ends, across the clock restart");
}

// The voice: a strobed vowel makes sound, and STOP goes quiet
static double rmsOf(Mockingboard *mb, int samples)
{
  static int16_t buf[SAMPLE_RATE];
  mb->renderToBuffer(buf, samples);
  double sq = 0;
  for (int i = 0; i < samples; i++) sq += (double)buf[i] * buf[i];
  return sqrt(sq / samples);
}

static void testVoice()
{
  Mockingboard ss(true);
  ss.Reset();
  g_cpu->Reset();
  runTo(&ss, 1000);
  double quiet = rmsOf(&ss, SAMPLE_RATE / 10);
  check(quiet < 50, "the SC-01 starts quiet");

  ss.writeSlotRom(DDRB, 0xFF);
  ss.writeSlotRom(PCR, 0xB0);
  ss.writeSlotRom(ORB, 0x08);          // AH2
  double vowel = rmsOf(&ss, SAMPLE_RATE / 10);
  check(vowel > 1000, "a strobed vowel makes sound");

  ss.writeSlotRom(ORB, 0x3F);          // STOP
  rmsOf(&ss, SAMPLE_RATE / 5);
  double after = rmsOf(&ss, SAMPLE_RATE / 20);
  check(after < 50, "STOP goes quiet again");

  // With the strobe off, port B writes aren't phonemes
  ss.writeSlotRom(PCR, 0x00);
  ss.writeSlotRom(ORB, 0x08);
  check(rmsOf(&ss, SAMPLE_RATE / 10) < 50, "no strobe, no speech");
}

int main()
{
  g_cpu = &cpu;
  g_cpu->Reset();

  testLayout();
  testHandshake();
  testNoInterruptWhenMasked();
  testFallingEdge();
  testWiredOr();
  testNmi();
  testTimers();
  testAyVariants();
  testCtrlReset();
  testVoice();

  if (failures) {
    printf("test-soundspeech: %d failure(s)\n", failures);
    return 1;
  }
  printf("test-soundspeech: all checks passed\n");
  return 0;
}
