#include "sy6522.h"
#include <stdio.h>

#include "globals.h"

SY6522::SY6522()
{
  ORB = ORA = 0;
  DDRB = DDRA = 0x00;
  T1_CTR = T2_CTR = 0;
  T1_CTR_LATCH = T2_CTR_LATCH = 0;
  ACR = 0x20; // free-running; FIXME: constant?
  PCR = 0xB0; // FIXME: ?
  IFR = 0x00; // FIXME: ?
  IER = 0x90; // FIXME: ?
}
  
uint8_t SY6522::read(uint8_t address)
{
  switch (address) {
  case SY_ORB:
    return ORB;
  case SY_ORA:
    return ORA;
  case SY_DDRB:
    return DDRB;
  case SY_DDRA:
    return DDRA;
  case SY_TMR1L:
    IFR &= ~SY_IR_TIMER1;
    return (T1_CTR & 0xFF);
  case SY_TMR1H:
    return (T1_CTR >> 8);
  case SY_TMR1LL:
    return (T1_CTR_LATCH & 0xFF);
  case SY_TMR1HL:
    return (T1_CTR_LATCH >> 8);
  case SY_TMR2L:
    IFR &= ~SY_IR_TIMER2;
    return (T2_CTR & 0xFF);
  case SY_TMR2H:
    return (T2_CTR >> 8);
  case SY_SS:
    // FIXME: floating
    return 0xFF;
  case SY_ACR:
    return ACR;
  case SY_PCR:
    return PCR;
  case SY_IFR:
    return IFR;
  case SY_IER:
    return 0x80 | IER;
  case SY_ORANOHS:
    return ORA;
  }
  return 0xFF;
}

  void SY6522::write(uint8_t address, uint8_t val)
{
  switch (address) {
  case SY_ORB:
    val &= DDRB;
    ORB = val;
    ay8910[0].write(val, ORA & DDRA);
    return;

  case SY_ORA:
    ORA = val & DDRA;
    return;

  case SY_DDRB:
    DDRB = val;
    return;

  case SY_DDRA:
    DDRA = val;
    return;

  case SY_TMR1L:
  case SY_TMR1LL:
    T1_CTR_LATCH = (T1_CTR_LATCH & 0xFF00) | val;
    return;

  case SY_TMR1H:
    IFR &= SY_IR_TIMER1;

    // Update IFR?
    IFR &= 0x7F;
    if (IFR & IER) {
      // an interrupt is happening; keep the IE flag on
      IFR |= 0x80;
    }

    // FIXME: clear interrupt flag
    T1_CTR_LATCH = (T1_CTR_LATCH & 0x00FF) | (val << 8);
    T1_CTR = T1_CTR_LATCH;
    // FIXME: start timer?
    return;

  case SY_TMR1HL:
    T1_CTR_LATCH = (T1_CTR_LATCH & 0x00FF) | (val << 8);
    // FIXME: clear interrupt flag
    return;

  case SY_TMR2L:
    T2_CTR_LATCH = (T2_CTR_LATCH & 0xFF00) | val;
    return;

  case SY_TMR2H:
    // FIXME: clear timer2 interrupt flag
    T2_CTR_LATCH = (T2_CTR_LATCH & 0x00FF) | (val << 8);
    T2_CTR = T2_CTR_LATCH;
    return;

  case SY_SS:
    // FIXME: what is this for?
    return;

  case SY_ACR:
    ACR = val;
    break;

  case SY_PCR:
    PCR = val;
    break;

  case SY_IFR:
    // Clear whatever low bits are set in IFR.
    val |= 0x80;
    val ^= 0x7F;
    IFR &= val;
    break;

  case SY_IER:
    if (val & 0x80) {
      // Set bits based on val
      val &= 0x7F;
      IER |= val;
      // FIXME: start timer if necessary?
    } else {
      // Clear whatever low bits are set in IER.
      val |= 0x80;
      val ^= 0x7F;
      IER &= val;
      // FIXME: stop timer if it's running?
    }
    return;
    
  case SY_ORANOHS:
    // FIXME: what is this for?
    return;
  }
}

void SY6522::update(uint32_t cycles)
{
  /* Update 6522 timers */

  // ...
  /*  
  SY_IR_CA2       = 1,
    SY_IR_CA1       = 2,
    SY_IR_SHIFTREG  = 4,
    SY_IR_CB2       = 8,
    SY_IR_CB1       = 16,
    SY_IR_TIMER2    = 32,
    SY_IR_TIMER1    = 64,
    SY_IER_SETCLEAR = 128,
  SY_IFR_IRQ      = 128
  */
  /* Check for 6522 interrupts */
  if (IFR & 0x80) {
    g_cpu->stageIRQ();
  }

  /* Update the attached 8910 chip(s) */
  ay8910[0].update(cycles);
}

