#ifndef _PIA6821_H
#define _PIA6821_H

#include <stdint.h>

// http://webpages.charter.net/coinopcauldron/piaarticle.html

#define DDRA 0
#define CTLA 1
#define DDRB 2
#define CTLB 3

class PIA6821 {
 public:
  PIA6821();
  ~PIA6821();

  uint8_t read(uint8_t addr);


 private:
  uint8_t porta, portb;
  uint8_t ddra, ddrb;
  uint8_t cra, crb; // control registers
  /*
  2 ports - porta, portb
  */

};

#endif
