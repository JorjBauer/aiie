#ifndef __PARALLELSRAM_H
#define __PARALLELSRAM_H

#include <Arduino.h>

class ParallelSRAM {
 public:
  ParallelSRAM();
  ~ParallelSRAM();

  void SetPins();

  uint8_t read(uint32_t addr);
  void write(uint32_t addr, uint8_t v);

 protected:
  uint8_t getInput();
  void setOutput(uint8_t v);
  void setAddress(uint32_t addr);

 private:
  bool isInput;
};

#endif
