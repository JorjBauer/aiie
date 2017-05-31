#ifndef __SY6522_H
#define __SY6522_H

#include <stdint.h>

#include "ay8910.h"

// 6522 interface registers
enum {
  SY_ORB     = 0x00, // ORB
  SY_ORA     = 0x01, // ORA
  SY_DDRB    = 0x02, // DDRB
  SY_DDRA    = 0x03, // DDRA
  SY_TMR1L   = 0x04, // TIMER1L_COUNTER
  SY_TMR1H   = 0x05, // TIMER1H_COUNTER
  SY_TMR1LL  = 0x06, // TIMER1L_LATCH
  SY_TMR1HL  = 0x07, // TIMER1H_LATCH
  SY_TMR2L   = 0x08, // TIMER2L
  SY_TMR2H   = 0x09, // TIMER2H
  SY_SS      = 0x0a, // SERIAL_SHIFT
  SY_ACR     = 0x0b, // ACR
  SY_PCR     = 0x0c, // PCR
  SY_IFR     = 0x0d, // IFR
  SY_IER     = 0x0e, // IER
  SY_ORANOHS = 0x0f  // ORA_NO_HS
};

// IFR and IER share the names of all but the high bit
enum {
  SY_IR_CA2       = 1,
  SY_IR_CA1       = 2,
  SY_IR_SHIFTREG  = 4,
  SY_IR_CB2       = 8,
  SY_IR_CB1       = 16,
  SY_IR_TIMER2    = 32,
  SY_IR_TIMER1    = 64,
  SY_IER_SETCLEAR = 128,
  SY_IFR_IRQ      = 128
};

class SY6522 {
 public:
  SY6522();
  
  uint8_t read(uint8_t address);
  void write(uint8_t address, uint8_t val);

  void update(uint32_t cycles);

 private:
  uint8_t ORB;             // port B
  uint8_t ORA;             // port A
  uint8_t DDRB;            // data direction register
  uint8_t DDRA;            //
  uint16_t T1_CTR;         // counters
  uint16_t T1_CTR_LATCH;
  uint16_t T2_CTR;
  uint16_t T2_CTR_LATCH;
  uint8_t ACR;             // Aux Control Register
  uint8_t PCR;             // Peripheral Control Register
  uint8_t IFR;             // Interrupt Flag Register
  uint8_t IER;             // Interrupt Enable Register

  AY8910 ay8910[1]; // FIXME: an array in case we support more than one ... ?
};

#endif
