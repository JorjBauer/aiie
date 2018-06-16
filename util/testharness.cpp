#include <stdio.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#include "cpu.h"
#include "mmu.h"

bool running = true;
bool verbose = false;
unsigned long startpc = 0x400;

class TestMMU : public MMU {
public:
  TestMMU() {}
  virtual ~TestMMU() {}

  virtual void Reset() {}

  virtual uint8_t read(uint16_t mem) { if (mem == 0xBFF0 || mem == 0xF001) { return 'R'; } return ram[mem];}
  virtual void write(uint16_t mem, uint8_t val) {
    if (mem == 0xBFF0 || mem == 0xF001) {printf("%c", val); return;} 
    if (mem == 0x202 || mem == 0x200) {
      if (val == 240) { printf("All tests successful!\n"); running = 0; }
      printf("Start test %d\n", val);
    }
    ram[mem] = val;}
  virtual uint8_t readDirect(uint16_t address, uint8_t fromPage) { return read(address);}

  virtual bool Serialize(int8_t fd) { return false; }
  virtual bool Deserialize(int8_t fd) { return false; }

  uint8_t ram[65536];
};

class FileManager;

FileManager *g_filemanager = NULL;
Cpu cpu;
TestMMU mmu;

int main(int argc, char *argv[])
{
  int ch;
  int fd = -1;

  while ((ch = getopt(argc, argv, "f:vs:")) != -1) {
    switch (ch) {
    case 's':
      if (optarg[0] == '0' &&
	  optarg[1] == 'x') {
	startpc = strtol(optarg+2, NULL, 16);
      } else {
	startpc = strtol(optarg, NULL, 10);
      }
      break;
    case 'v':
      verbose = true;
      break;
    case 'f':
      {
	if ((fd = open(optarg, O_RDONLY, 0)) < 0) {
	  fprintf(stderr, "%s: %s\n", optarg, strerror(errno));
	  exit(1);
	}
      }
      break;
    }
  }

  if (fd == -1) {
    fprintf(stderr, "Missing '-f <filename>'\n");
    exit(1);
  }

  cpu.SetMMU(&mmu);
  cpu.rst();


  ssize_t s;
  char c;
  unsigned long pos = 0;
  while ((s = read(fd, &c, 1)) == 1) {
    mmu.ram[pos] = c;
    pos ++;
  }

  cpu.pc = startpc;
  //  cpu.Reset();

  time_t startTime = time(NULL);
  // call cpu.Run() in the worst possible way (most overhead)
  for (uint32_t i=0; running && i<1000000000; i++) {
    if (cpu.pc == 0x453a) {
      printf("Somehow wound up at input routine; exiting\n");
      exit(1);
    }

    int o = mmu.read(cpu.pc);
    if (o == 0xDB) {
      // end of the decimal mode tests
      int result = mmu.read(0x0b);
      printf("Test complete. Result: %s\n", result ? "failed" : "passed");
      exit(result);
    }

    cpu.Run(1);
    
    if (verbose) {
      printf("time %u PC $%.4X OP $%.2X mem200 #%d mem202 #%d X 0x%.2X Y 0x%.2X A 0x%.2X SP 0x%.2X Status 0x%.2X\n", cpu.cycles, cpu.pc, mmu.read(cpu.pc), mmu.read(0x200), mmu.read(0x202), cpu.x, cpu.y, cpu.a, cpu.sp, cpu.flags);
    }
  }
  time_t endTime = time(NULL);

  printf("%ld seconds\n", endTime - startTime);
  printf("Ending PC: 0x%X\n", cpu.pc);
  
}
