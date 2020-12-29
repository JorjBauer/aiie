#include "pia6821.h"

PIA6821::PIA6821()
{
}

PIA6821::~PIA6821()
{
}

uint8_t PIA6821::read(uint8_t addr)
{
  uint8_t rv;

  switch (addr) {
  case DDRA:
    if (cra & 0x04) { // DDR or Peripherial Interface access control
      // peripheral
      //      rv = readPeripheralA();
      // FIXME continue here
    } else {
      rv = ddra;
    }
    break;
  case CTLA:
    rv = cra;
    break;
  case DDRB:
    if (crb & 0x04) {
      //      rv = readPeripheralB();
      // FIXME continue here
    } else {
      rv = ddrb;
    }
    break;
  case CTLB:
    rv = crb;
    break;
  }

  return rv;
}

