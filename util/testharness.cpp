#include <stdio.h>
#include <time.h>
#include <string.h>

#include "cpu.h"
#include "mmu.h"

#ifdef BASICTEST
#include "6502_functional_test.h"
#elif defined(VERBOSETEST)
#include "6502_functional_test_2.h"
#elif defined(EXTENDEDTEST)
#include "65C02_extended_opcodes_test.h"
#else
#error undefined test - specify one of BASICTEST, VERBOSETEST, or EXTENDEDTEST
#endif

class TestMMU : public MMU {
public:
  TestMMU() {}
  virtual ~TestMMU() {}

  virtual void Reset() {}

  virtual uint8_t read(uint16_t mem) { if (mem == 0xBFF0) { return 'R'; } return ram[mem];}
  virtual void write(uint16_t mem, uint8_t val) {
    if (mem == 0xBFF0) {printf("%c", val); return;} 
    if (mem == 0x200) {printf("Start test %d\n", val);}
    ram[mem] = val;}
  virtual uint8_t readDirect(uint16_t address, uint8_t fromPage) { return read(address);}

  uint8_t ram[65536];
};

Cpu cpu;
TestMMU mmu;

int main(int argc, char *argv[])
{
  cpu.SetMMU(&mmu);
  cpu.rst();

  // Load the 6502 functional test
  memcpy(mmu.ram, functest, 0x10000);
  cpu.pc = 0x400;
  //  cpu.Reset();

  time_t startTime = time(NULL);
  // call cpu.Run() in the worst possible way (most overhead)
  for (uint32_t i=0; i<1000000000; i++) {
    cpu.Run(1);
    
#if 0
    if (cpu.pc < 0x477F) {
      printf("%llu OP $%.2X #%d 0x%.2X X 0x%.2X Y 0x%.2X A 0x%.2X SP 0x%.2X S 0x%.2X\n", cpu.cycles, mmu.read(cpu.pc), mmu.read(0x200), cpu.pc, cpu.x, cpu.y, cpu.a, cpu.sp, cpu.flags);
    }
#endif
  }
  time_t endTime = time(NULL);

  printf("%ld seconds\n", endTime - startTime);
  printf("Ending PC: 0x%X\n", cpu.pc);
  
}
