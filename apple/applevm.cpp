#include "applevm.h"
#include "filemanager.h"
#include "cpu.h"

#include "appledisplay.h"
#include "applekeyboard.h"
#include "physicalkeyboard.h"

#include "globals.h"

#ifdef TEENSYDUINO
#include "teensy-println.h"
#endif

#include <errno.h>
const char *suspendHdr = "Sus2";

AppleVM::AppleVM()
{
  // FIXME: all this typecasting makes me knife-stabby
  vmdisplay = new AppleDisplay();
  mmu = new AppleMMU((AppleDisplay *)vmdisplay);
  vmdisplay->SetMMU((AppleMMU *)mmu);

  disk6 = new DiskII((AppleMMU *)mmu);
  ((AppleMMU *)mmu)->setSlot(6, disk6);

  keyboard = new AppleKeyboard((AppleMMU *)mmu);

  parallel = new ParallelCard();
  ((AppleMMU *)mmu)->setSlot(1, parallel);

  hd32 = new HD32((AppleMMU *)mmu);
  ((AppleMMU *)mmu)->setSlot(7, hd32);

  mouse = new Mouse();
  ((AppleMMU *)mmu)->setSlot(4, mouse);
}

AppleVM::~AppleVM()
{
  delete disk6;
  delete parallel;
}

void AppleVM::Suspend(const char *fn)
{
  /* Open a new suspend file via the file manager; tell all our
     objects to serialize in to it; close the file */

  int8_t fh = g_filemanager->openFile(fn);
  if (fh == -1) {
    // Unable to open; skip suspend
    return;
  }

  /* Header */
  if (g_filemanager->write(fh, suspendHdr, strlen(suspendHdr)) != strlen(suspendHdr))
    return;

  /* Tell all of the peripherals to suspend */
  if (g_cpu->Serialize(fh) &&
      disk6->Serialize(fh) &&
      hd32->Serialize(fh)
      ) {
#ifdef TEENSYDUINO
    println("All serialized successfully");
#else
    printf("All serialized successfully\n");
#endif
  }

  g_filemanager->closeFile(fh);
}

void AppleVM::Resume(const char *fn)
{
  /* Open the given suspend file via the file manager; tell all our
     objects to deserialize from it; close the file */

  int8_t fh = g_filemanager->openFile(fn);
  if (fh == -1) {
    // Unable to open; skip resume
#ifdef TEENSYDUINO
    print("Unable to open resume file ");
    println(fn);
#else
    printf("Unable to open resume file\n");
#endif
    g_filemanager->closeFile(fh);
    return;
  }

  /* Header */
  uint8_t c;
  for (int i=0; i<strlen(suspendHdr); i++) {
    if (g_filemanager->read(fh, &c, 1) != 1 ||
	c != suspendHdr[i]) {
      /* Failed to read correct header; abort */
      g_filemanager->closeFile(fh);
      return;
    }
  }

  /* Tell all of the peripherals to resume */
  if (g_cpu->Deserialize(fh) &&
      disk6->Deserialize(fh) &&
      hd32->Deserialize(fh)
      ) {
#ifdef TEENSYDUINO
    println("Deserialization successful");
#else
    printf("All deserialized successfully\n");
#endif
  } else {
#ifndef TEENSYDUINO
    printf("Deserialization failed\n");
    exit(1);
#endif
  }

  g_filemanager->closeFile(fh);
}

// fixme: make member vars
unsigned long paddleCycleTrigger[2] = {0, 0};

void AppleVM::triggerPaddleInCycles(uint8_t paddleNum,uint16_t cycleCount)
{
  paddleCycleTrigger[paddleNum] = cycleCount + g_cpu->cycles;
}

void AppleVM::cpuMaintenance(int64_t cycles)
{
  for (uint8_t i=0; i<2; i++) {
    if (paddleCycleTrigger[i] && cycles >= paddleCycleTrigger[i]) {
      ((AppleMMU *)mmu)->triggerPaddleTimer(i);
      paddleCycleTrigger[i] = 0;
    }
  }

  keyboard->maintainKeyboard(cycles);
  disk6->maintenance(cycles);
}

void AppleVM::Reset()
{
  disk6->Reset();
  ((AppleMMU *)mmu)->resetRAM();
  mmu->Reset();

  g_cpu->pc = (((AppleMMU *)mmu)->read(0xFFFD) << 8) | ((AppleMMU *)mmu)->read(0xFFFC);

  // give the keyboard a moment to depress keys upon startup
  keyboard->maintainKeyboard(0);
}

void AppleVM::Monitor()
{
  g_cpu->pc = 0xff69; // "call -151"                                                                             
  ((AppleMMU *)mmu)->readSwitches(0xC054); // make sure we're in page 1                                                      
  ((AppleMMU *)mmu)->readSwitches(0xC056); // and that hires is off                                                          
  ((AppleMMU *)mmu)->readSwitches(0xC051); // and text mode is on                                                            
}

const char *AppleVM::DiskName(uint8_t drivenum)
{
  return disk6->DiskName(drivenum);
}

void AppleVM::ejectDisk(uint8_t drivenum)
{
  disk6->ejectDisk(drivenum);
}

void AppleVM::insertDisk(uint8_t drivenum, const char *filename, bool drawIt)
{
  disk6->insertDisk(drivenum, filename, drawIt);
}

const char *AppleVM::HDName(uint8_t drivenum)
{
  return hd32->diskName(drivenum);
}

void AppleVM::ejectHD(uint8_t drivenum)
{
  hd32->ejectDisk(drivenum);
}

void AppleVM::insertHD(uint8_t drivenum, const char *filename)
{
  hd32->insertDisk(drivenum, filename);
}

VMKeyboard * AppleVM::getKeyboard()
{
  return keyboard;
}
