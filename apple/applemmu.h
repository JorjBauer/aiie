#ifndef __APPLEMMU_H
#define __APPLEMMU_H

#include <stdlib.h>
#include "appledisplay.h"
#include "slot.h"
#include "mmu.h"
#include "noslotclock.h"
#include "debugger.h"

// Reading a soft switch that drives nothing onto the data bus returns
// whatever the video scanner last put there: see AppleMMU::floatingBus().
// This used to be the constant 0, with a note wondering how to return
// what the Apple would have. It now does.

// A card can leave the floating bus on the data bus too (a Disk II read of
// an odd address does: UTA2E Table 9.1, note 2), and a card has no MMU
// pointer, so the macro goes through this rather than through a member.
// It answers 0 before there is a machine, which is what a bench with no
// MMU wants.
uint8_t appleFloatingBus();

#define _FLOATINGBUS appleFloatingBus()

// Switches activated by various memory locations
enum {
  S_TEXT  = 0x0001,
  S_MIXED = 0x0002,
  S_HIRES = 0x0004,
  S_PAGE2 = 0x0008,
  S_80COL = 0x0010,
  S_ALTCH = 0x0020,
  S_80STORE = 0x0040,
  S_DHIRES = 0x0080
};

typedef bool (*callback_t)(void *);

class AppleVM;

class AppleMMU : public MMU {
  friend class AppleVM;

 public:
  AppleMMU(AppleDisplay *display);
  virtual ~AppleMMU();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual uint8_t read(uint16_t address);
  virtual uint8_t readDirect(uint16_t address, uint8_t fromPage);
  virtual void write(uint16_t address, uint8_t v);

  // Side-effect-free access for the debugger. Never touches a soft switch.
  // For $C000 to $C0FF, peek returns the value a status read would give at
  // the read-only status locations ($C010 to $C01F, $C061 to $C063), the
  // keyboard latch for $C000 to $C00F, and 0 elsewhere; poke there does
  // nothing. For $C100 to $CFFF in DBG_BANK_CPU, peek returns what the CPU
  // would fetch from ROM (a card that answers those addresses with live
  // registers rather than a ROM reads as 0, so that peeking never pokes a
  // chip).
  uint8_t peek(uint16_t address, const DebugBank &bank);
  void    poke(uint16_t address, uint8_t v, const DebugBank &bank);

  // The video soft switches as a word of S_* bits.
  uint16_t videoSwitches() { return switches; }
  uint16_t auxBankCount() { return numAuxBanks; }

  virtual void Reset();

  void keyboardInput(uint8_t v);
  void setKeyDown(bool isTrue);

  // Scripted/automation keyboard support (see AppleKeyboard's type-ahead queue
  // and the desktop debugger's 'K' command). injectKeypress() latches a key
  // exactly like a real press but does NOT hold anyKeyDown (a queued keystroke
  // is a momentary tap). keyboardStrobePending() reports whether a latched key
  // is still waiting to be read, so injection can be paced one key at a time.
  void injectKeypress(uint8_t v);
  bool keyboardStrobePending();

  void triggerPaddleTimer(uint8_t paddle);

  void resetRAM(); // used by BIOS on cold boot

  void setSlot(int8_t slotnum, Slot *peripheral);
  void clearSlotRom(int8_t slotnum);

  // Configure RamWorks-compatible aux expansion. 'megabytes' is the
  // total aux size (0=none/stock, 1, 3, or 16). (Re)allocates the
  // expansion buffer and resets the selected bank to 0.
  void setRamworksSize(uint8_t megabytes);

  void setAppleKey(int8_t which, bool isDown);

  // Video soft-switch transition log, for scanline-accurate rendering of
  // mid-frame ("VBL-timed") mode changes. logVideoState() records the
  // current switches word with a cycle stamp whenever it has changed;
  // switchesAtCycle() answers what the switches were at a given cycle.
  void logVideoState();
  uint16_t switchesAtCycle(int64_t cyc);

  // What the video scanner is fetching this cycle: the floating bus. See
  // the implementation in applemmu.cpp.
  uint8_t floatingBus();
  uint16_t videoScannerAddress(int64_t cyc);

 protected:
  uint8_t readCore(uint16_t address);
  bool debugLocate(uint16_t address, const DebugBank &bank, bool forWrite,
		   uint8_t **ramworks, uint32_t *ramAddr);
  bool handleNoSlotClock(uint16_t address, uint8_t *rv);

  void resetDisplay();
  uint8_t readSwitches(uint16_t address);
  void writeSwitches(uint16_t address, uint8_t v);
  void handleMemorySwitches(uint16_t address, uint16_t lastSwitch);

  void updateMemoryPages();

 private:
  AppleDisplay *display;
  uint16_t switches;

  // Rolling ring of video-switch transitions. Sized to hold more than one
  // video frame's worth of changes even for a demo that flips modes every
  // scanline (a frame is 262 lines); the display samples at ~30Hz, so a
  // couple of frames can accumulate between redraws.
  static const int kVideoLogSize = 1024;
  struct VideoXsn { int64_t cyc; uint16_t sw; };
  VideoXsn videoLog[kVideoLogSize];
  int videoLogHead;              // index of the next slot to write
  int videoLogCount;             // valid entries, capped at kVideoLogSize
  uint16_t lastLoggedSwitches;   // dedup: only log real changes
 public: // 'public' for debugging
  bool auxRamRead;
  bool auxRamWrite;
  bool bank2;
  bool readbsr;
  bool writebsr;
  bool altzp;
  bool intcxrom;
  bool slot3rom;
  int8_t slotLatch;

  bool preWriteFlag;        // see UTA2E p. 5-23

  Slot *slots[8]; // slots 0-7

  // RamWorks-compatible aux expansion state.
  uint8_t auxBank;        // currently-selected aux bank ($C073); 0 = stock aux
  uint16_t numAuxBanks;   // total number of 64K aux banks (1 = stock 80-col card)
  uint8_t *auxExpansion;  // banks 1..numAuxBanks-1, 64K each; NULL when none

  uint16_t readPages[0x100];
  uint16_t writePages[0x100];

  // For each logical page, whether the current read/write mapping points
  // at auxiliary memory (and is therefore subject to RamWorks banking).
  bool readPageIsAux[0x100];
  bool writePageIsAux[0x100];

  bool anyKeyDown;
  
  NoSlotClock *clock;
};

#endif
