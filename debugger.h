#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include <stdint.h>

// The platform-free debugger core. See debugger-interface.md.
//
// Every function here is called from the emulator thread only, between
// instructions. Nothing prints, blocks or waits: a front end (the SDL
// build's debug socket, the Electron build's window) drives this API and
// formats the answers itself.
//
// Builds that do not define AIIE_DEBUGGER (the Teensy and FPGA firmware,
// the bench tests) get the no-op class at the bottom of this file, so the
// hooks in Cpu::step() and AppleMMU compile to nothing.

enum DebugRunState { DBG_RUNNING, DBG_PAUSED };

enum DebugHaltReason {
  DBG_HALT_NONE,        // no halt has happened since the last takeHalt()
  DBG_HALT_PAUSE,       // pause() was called
  DBG_HALT_STEP,        // a step completed
  DBG_HALT_BREAKPOINT,  // PC reached a breakpoint (addr is the PC)
  DBG_HALT_WATCHPOINT,  // an access hit a watchpoint (addr, isWrite, value)
  DBG_HALT_RUNTO,       // runTo() reached its address
};

struct DebugHalt {
  DebugHaltReason reason;
  uint16_t addr;        // breakpoint or watchpoint address, or the PC for a step
  uint16_t pc;          // PC at the halt, which is the next instruction to run
  bool isWrite;         // watchpoints only
  uint8_t value;        // watchpoints only: the value read or written
};

#define DBG_MAX_BREAKPOINTS 32
#define DBG_MAX_WATCHPOINTS 16
#define DBG_TRACE_DEPTH 10000

struct DebugBreakpoint { uint16_t addr; bool enabled; };
struct DebugWatchpoint { uint16_t from, to; bool onRead, onWrite; bool enabled; };

// Which memory a peek or poke means. The //e has main RAM, aux RAM
// (several banks with RamWorks), two language card banks at $D000, and
// ROM; a debugger must be able to read any of them without disturbing a
// soft switch.
enum DebugBankKind {
  DBG_BANK_CPU,   // whatever the CPU sees now, switches as they stand
  DBG_BANK_MAIN,  // main RAM
  DBG_BANK_AUX,   // aux RAM; auxBank selects the RamWorks bank, 0 is the stock 64K
  DBG_BANK_ROM,   // the ROM image for $C100 to $FFFF, regardless of switches
};

struct DebugBank {
  DebugBankKind kind;
  uint8_t auxBank;   // DBG_BANK_AUX only: 0 .. numAuxBanks-1
  uint8_t lcBank;    // 1 or 2: which language card bank answers for $D000 to $DFFF in MAIN and AUX
};

#ifdef AIIE_DEBUGGER

class Debugger {
 public:
  Debugger();

  // ---- run control ----
  DebugRunState state() { return runState; }
  void pause();                 // halt before the next instruction
  void cont();                  // resume; steps over a breakpoint at the current PC
  void stepIn();                // one instruction
  void stepOver();              // one instruction, but a JSR runs until it returns
  void stepOut();               // run until the current subroutine returns (RTS or RTI)
  void runTo(uint16_t addr);    // run until PC == addr
  bool takeHalt(DebugHalt *out); // true once per halt, then clears it

  // ---- breakpoints and watchpoints ----
  int  addBreakpoint(uint16_t addr);            // index, or -1 when full or duplicate
  bool removeBreakpoint(uint16_t addr);
  void enableBreakpoint(int index, bool on);
  void clearBreakpoints();
  int  breakpointCount();
  const DebugBreakpoint *breakpoint(int index);

  int  addWatchpoint(uint16_t from, uint16_t to, bool onRead, bool onWrite);
  bool removeWatchpoint(int index);
  void enableWatchpoint(int index, bool on);
  void clearWatchpoints();
  int  watchpointCount();
  const DebugWatchpoint *watchpoint(int index);

  // ---- memory ----
  uint8_t  peek(uint16_t addr, const DebugBank &bank);
  void     poke(uint16_t addr, uint8_t v, const DebugBank &bank);
  void     peekRange(uint16_t addr, uint8_t *out, uint16_t len, const DebugBank &bank);
  void     pokeRange(uint16_t addr, const uint8_t *in, uint16_t len, const DebugBank &bank);

  // ---- registers ----
  // Read g_cpu->a, x, y, sp, pc, flags, cycles directly. Write them through
  // here so that stepping state is reset when the PC moves.
  void setPC(uint16_t pc);
  void setRegisters(uint8_t a, uint8_t x, uint8_t y, uint8_t sp, uint8_t flags);

  // ---- disassembly ----
  // Disassembles one instruction at addr in bank into out (mnemonic and
  // operands, no address prefix). Returns the instruction length in bytes.
  uint8_t disassemble(uint16_t addr, const DebugBank &bank, char *out, uint16_t outSize);

  // ---- trace ----
  void     setTrace(bool on);
  bool     tracing() { return traceOn; }
  uint32_t traceCount();                   // entries available, up to DBG_TRACE_DEPTH
  uint16_t traceEntry(uint32_t back);      // PC of the instruction `back` steps ago; 0 is the last executed

  // ---- machine state ----
  uint16_t softSwitches();                 // AppleMMU::switches, bits named S_* in applemmu.h
  // The text screen as the display would show it: 24 rows of 40 or 80
  // ASCII characters, folded from screen codes. Returns the column count.
  // base forces a 40-column dump of that page; 0 means detect the mode
  // from the video switches.
  uint8_t  textScreen(char out[24][80], uint16_t base);

  // ---- keyboard injection ----
  // Queues keystrokes for the running program, with backslash escapes: \r
  // and \n are Return, \t tab, \e Escape, \0 NUL, \\ backslash, \xHH any
  // byte. Returns the count queued, which is less than the input length
  // when the queue (256) fills.
  uint16_t injectKeys(const char *s);
  uint16_t injectQueueDepth();

  // ---- hooks called by the machine. Inline and cheap when nothing is armed. Not for hosts. ----
  bool armed() { return isArmed; }                   // any breakpoint, watchpoint, pending step, pause, or trace
  bool onInstruction();                              // before each instruction; true means halt now
  void onAccess(uint16_t addr, uint8_t v, bool isWrite); // from AppleMMU when watchCount > 0

  // Read by AppleMMU on every access: one integer test on the hot path.
  uint16_t watchCount;

 private:
  enum StepMode { STEP_NONE, STEP_IN, STEP_OUT, STEP_RUNTO };

  void rearm();
  void halt(DebugHaltReason reason, uint16_t addr);
  void resume(StepMode mode);
  int  breakpointIndex(uint16_t addr);

  DebugRunState runState;
  bool isArmed;

  // The halt waiting for takeHalt(), if any.
  bool haltAvailable;
  DebugHalt lastHalt;

  // A halt requested for the next instruction boundary: pause(), or a
  // watchpoint that fired mid-instruction (its details are in requested).
  bool haltRequested;
  DebugHalt requested;

  // The first onInstruction() after a resume executes the instruction at
  // PC without checking it against the breakpoints: that is how cont()
  // gets past the breakpoint it stopped on.
  bool resuming;

  StepMode stepMode;
  DebugHaltReason stepReason;   // what a completed step reports (STEP or RUNTO)
  uint16_t runToAddr;
  uint8_t  stepOutSP;           // SP when stepOut() started; done once an RTS/RTI leaves us above it
  uint8_t  lastOpcode;          // opcode of the instruction that just executed

  DebugBreakpoint breakpoints[DBG_MAX_BREAKPOINTS];
  int numBreakpoints;
  DebugWatchpoint watchpoints[DBG_MAX_WATCHPOINTS];
  int numWatchpoints;

  bool traceOn;
  uint16_t trace[DBG_TRACE_DEPTH];
  uint32_t traceHead;   // next slot to write
  uint32_t traceFill;   // valid entries, capped at DBG_TRACE_DEPTH
};

extern Debugger g_debugger;

#else // !AIIE_DEBUGGER

// No debugger in this build: the hooks fold away.
class Debugger {
 public:
  Debugger() : watchCount(0) {}
  DebugRunState state() { return DBG_RUNNING; }
  bool armed() { return false; }
  bool onInstruction() { return false; }
  void onAccess(uint16_t, uint8_t, bool) {}
  uint16_t watchCount;
};

static Debugger g_debugger __attribute__((unused));

#endif // AIIE_DEBUGGER

#endif
