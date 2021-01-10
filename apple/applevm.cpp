#include "applevm.h"
#include "filemanager.h"
#include "cpu.h"

#include "appledisplay.h"
#include "applekeyboard.h"
#include "physicalkeyboard.h"

#include "serialize.h"

#include "globals.h"

#ifdef TEENSYDUINO
#include "teensy-println.h"
#include "iocompat.h"
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

bool AppleVM::Suspend(const char *fn)
{
  /* Open a new suspend file via the file manager; tell all our
     objects to serialize in to it; close the file */

  int8_t fd = g_filemanager->openFile(fn);
  if (fd == -1) {
    // Unable to open; skip suspend
    printf("failed to open suspend file\n");
    return false;
  }

  /* Header */
  serializeString(suspendHdr);

  /* Tell all of the peripherals to suspend */
  if (g_cpu->Serialize(fd) &&
      disk6->Serialize(fd) &&
      hd32->Serialize(fd)
      ) {
    printf("All serialized successfully\n");
  }

  g_filemanager->closeFile(fd);
  return true;
  
 err:
  g_filemanager->closeFile(fd);
  return false;
}

bool AppleVM::Resume(const char *fn)
{
  /* Open the given suspend file via the file manager; tell all our
     objects to deserialize from it; close the file */

  int8_t fd = g_filemanager->openFile(fn);
  if (fd == -1) {
    // Unable to open; skip resume
    printf("Unable to open resume file '%s'\n", fn);
    goto err;
  }

  /* Header */
  deserializeString(debugBuf);
  if (strcmp(debugBuf, suspendHdr)) {
    printf("Bad file header while resuming\n");
    goto err;
  }

  /* Tell all of the peripherals to resume */
  if (g_cpu->Deserialize(fd) &&
      disk6->Deserialize(fd) &&
      hd32->Deserialize(fd)
      ) {
    printf("All deserialized successfully\n");
  } else {
    printf("Deserialization failed\n");
#ifndef TEENSYDUINO
    exit(1);
#endif
    goto err;
  }

  g_filemanager->closeFile(fd);
  return true;
 err:
  g_filemanager->closeFile(fd);
  return false;
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
  mouse->maintainMouse(cycles);
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
