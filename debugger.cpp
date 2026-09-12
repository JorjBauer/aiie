#include "debugger.h"

#ifdef AIIE_DEBUGGER

#include <string.h>
#include "globals.h"
#include "cpu.h"
#include "applemmu.h"
#include "applekeyboard.h"
#include "disassembler.h"

Debugger g_debugger;

static Disassembler dbgDis;

static const DebugBank cpuBank = { DBG_BANK_CPU, 0, 1 };

static AppleMMU *theMMU()
{
  return g_vm ? (AppleMMU *)g_vm->getMMU() : NULL;
}

Debugger::Debugger()
{
  runState = DBG_RUNNING;
  isArmed = false;
  haltAvailable = false;
  haltRequested = false;
  resuming = false;
  stepMode = STEP_NONE;
  stepReason = DBG_HALT_STEP;
  runToAddr = 0;
  stepOutSP = 0;
  lastOpcode = 0;
  numBreakpoints = 0;
  numWatchpoints = 0;
  watchCount = 0;
  traceOn = false;
  traceHead = 0;
  traceFill = 0;
  memset(&lastHalt, 0, sizeof(lastHalt));
  memset(&requested, 0, sizeof(requested));
}

// ---- run control ----------------------------------------------------------

void Debugger::rearm()
{
  isArmed = (runState == DBG_PAUSED) || haltRequested || resuming ||
    (numBreakpoints > 0) || (stepMode != STEP_NONE) || traceOn;
}

void Debugger::halt(DebugHaltReason reason, uint16_t addr)
{
  runState = DBG_PAUSED;
  lastHalt.reason = reason;
  lastHalt.addr = addr;
  lastHalt.pc = g_cpu->pc;
  lastHalt.isWrite = false;
  lastHalt.value = 0;
  haltAvailable = true;
  haltRequested = false;
  stepMode = STEP_NONE;
  rearm();
}

void Debugger::resume(StepMode mode)
{
  runState = DBG_RUNNING;
  resuming = true;
  stepMode = mode;
  haltRequested = false;
  rearm();
}

void Debugger::pause()
{
  if (runState == DBG_PAUSED) return;
  requested.reason = DBG_HALT_PAUSE;
  requested.addr = g_cpu->pc;
  requested.isWrite = false;
  requested.value = 0;
  haltRequested = true;
  rearm();
}

void Debugger::cont()
{
  if (runState == DBG_RUNNING && stepMode == STEP_NONE && !haltRequested) return;
  resume(STEP_NONE);
}

void Debugger::stepIn()
{
  stepReason = DBG_HALT_STEP;
  resume(STEP_IN);
}

void Debugger::stepOver()
{
  if (peek(g_cpu->pc, cpuBank) == 0x20) {   // JSR: run until it comes back
    runToAddr = g_cpu->pc + 3;
    stepReason = DBG_HALT_STEP;
    resume(STEP_RUNTO);
  } else {
    stepIn();
  }
}

void Debugger::stepOut()
{
  stepOutSP = g_cpu->sp;
  lastOpcode = 0;
  stepReason = DBG_HALT_STEP;
  resume(STEP_OUT);
}

void Debugger::runTo(uint16_t addr)
{
  runToAddr = addr;
  stepReason = DBG_HALT_RUNTO;
  resume(STEP_RUNTO);
}

bool Debugger::takeHalt(DebugHalt *out)
{
  if (!haltAvailable) return false;
  if (out) *out = lastHalt;
  haltAvailable = false;
  return true;
}

// Called by Cpu::step() before every instruction while armed. The PC is the
// instruction about to run.
bool Debugger::onInstruction()
{
  if (runState == DBG_PAUSED) return true;

  uint16_t pc = g_cpu->pc;

  if (resuming) {
    // The instruction at PC runs no matter what: this is how cont() gets
    // past the breakpoint it stopped on.
    resuming = false;
    rearm();
  } else {
    if (haltRequested) {
      DebugHaltReason reason = requested.reason;
      halt(reason, requested.addr);
      lastHalt.isWrite = requested.isWrite;
      lastHalt.value = requested.value;
      return true;
    }

    int bi = breakpointIndex(pc);
    if (bi >= 0 && breakpoints[bi].enabled) {
      halt(DBG_HALT_BREAKPOINT, pc);
      return true;
    }

    switch (stepMode) {
    case STEP_NONE:
      break;
    case STEP_IN:
      halt(DBG_HALT_STEP, pc);
      return true;
    case STEP_OUT:
      // An RTS or RTI just executed and the stack is above where we
      // started: the subroutine we were in has returned.
      if ((lastOpcode == 0x60 || lastOpcode == 0x40) && g_cpu->sp > stepOutSP) {
	halt(DBG_HALT_STEP, pc);
	return true;
      }
      break;
    case STEP_RUNTO:
      if (pc == runToAddr) {
	halt(stepReason, pc);
	return true;
      }
      break;
    }
  }

  if (stepMode == STEP_OUT) lastOpcode = peek(pc, cpuBank);

  if (traceOn) {
    trace[traceHead] = pc;
    traceHead = (traceHead + 1) % DBG_TRACE_DEPTH;
    if (traceFill < DBG_TRACE_DEPTH) traceFill++;
  }

  return false;
}

// Called by AppleMMU::read and write when any watchpoint is set.
void Debugger::onAccess(uint16_t addr, uint8_t v, bool isWrite)
{
  if (runState == DBG_PAUSED || haltRequested) return;
  for (int i = 0; i < numWatchpoints; i++) {
    const DebugWatchpoint &w = watchpoints[i];
    if (!w.enabled) continue;
    if (addr < w.from || addr > w.to) continue;
    if (isWrite ? !w.onWrite : !w.onRead) continue;
    requested.reason = DBG_HALT_WATCHPOINT;
    requested.addr = addr;
    requested.isWrite = isWrite;
    requested.value = v;
    haltRequested = true;
    rearm();
    return;
  }
}

// ---- breakpoints ----------------------------------------------------------

int Debugger::breakpointIndex(uint16_t addr)
{
  for (int i = 0; i < numBreakpoints; i++) {
    if (breakpoints[i].addr == addr) return i;
  }
  return -1;
}

int Debugger::addBreakpoint(uint16_t addr)
{
  if (numBreakpoints >= DBG_MAX_BREAKPOINTS) return -1;
  if (breakpointIndex(addr) >= 0) return -1;
  breakpoints[numBreakpoints].addr = addr;
  breakpoints[numBreakpoints].enabled = true;
  numBreakpoints++;
  rearm();
  return numBreakpoints - 1;
}

bool Debugger::removeBreakpoint(uint16_t addr)
{
  int i = breakpointIndex(addr);
  if (i < 0) return false;
  for (; i < numBreakpoints - 1; i++) breakpoints[i] = breakpoints[i+1];
  numBreakpoints--;
  rearm();
  return true;
}

void Debugger::enableBreakpoint(int index, bool on)
{
  if (index < 0 || index >= numBreakpoints) return;
  breakpoints[index].enabled = on;
}

void Debugger::clearBreakpoints()
{
  numBreakpoints = 0;
  rearm();
}

int Debugger::breakpointCount()
{
  return numBreakpoints;
}

const DebugBreakpoint *Debugger::breakpoint(int index)
{
  if (index < 0 || index >= numBreakpoints) return NULL;
  return &breakpoints[index];
}

// ---- watchpoints ----------------------------------------------------------

int Debugger::addWatchpoint(uint16_t from, uint16_t to, bool onRead, bool onWrite)
{
  if (numWatchpoints >= DBG_MAX_WATCHPOINTS) return -1;
  if (to < from) { uint16_t t = from; from = to; to = t; }
  DebugWatchpoint &w = watchpoints[numWatchpoints];
  w.from = from;
  w.to = to;
  w.onRead = onRead;
  w.onWrite = onWrite;
  w.enabled = true;
  numWatchpoints++;
  watchCount = numWatchpoints;
  return numWatchpoints - 1;
}

bool Debugger::removeWatchpoint(int index)
{
  if (index < 0 || index >= numWatchpoints) return false;
  for (int i = index; i < numWatchpoints - 1; i++) watchpoints[i] = watchpoints[i+1];
  numWatchpoints--;
  watchCount = numWatchpoints;
  return true;
}

void Debugger::enableWatchpoint(int index, bool on)
{
  if (index < 0 || index >= numWatchpoints) return;
  watchpoints[index].enabled = on;
}

void Debugger::clearWatchpoints()
{
  numWatchpoints = 0;
  watchCount = 0;
}

int Debugger::watchpointCount()
{
  return numWatchpoints;
}

const DebugWatchpoint *Debugger::watchpoint(int index)
{
  if (index < 0 || index >= numWatchpoints) return NULL;
  return &watchpoints[index];
}

// ---- memory ---------------------------------------------------------------

uint8_t Debugger::peek(uint16_t addr, const DebugBank &bank)
{
  AppleMMU *mmu = theMMU();
  return mmu ? mmu->peek(addr, bank) : 0;
}

void Debugger::poke(uint16_t addr, uint8_t v, const DebugBank &bank)
{
  AppleMMU *mmu = theMMU();
  if (mmu) mmu->poke(addr, v, bank);
}

void Debugger::peekRange(uint16_t addr, uint8_t *out, uint16_t len, const DebugBank &bank)
{
  for (uint16_t i = 0; i < len; i++) out[i] = peek((uint16_t)(addr + i), bank);
}

void Debugger::pokeRange(uint16_t addr, const uint8_t *in, uint16_t len, const DebugBank &bank)
{
  for (uint16_t i = 0; i < len; i++) poke((uint16_t)(addr + i), in[i], bank);
}

// ---- registers ------------------------------------------------------------

void Debugger::setPC(uint16_t pc)
{
  g_cpu->pc = pc;
  // A step in progress was about a different instruction stream.
  stepMode = STEP_NONE;
  rearm();
}

void Debugger::setRegisters(uint8_t a, uint8_t x, uint8_t y, uint8_t sp, uint8_t flags)
{
  g_cpu->a = a;
  g_cpu->x = x;
  g_cpu->y = y;
  g_cpu->sp = sp;
  g_cpu->flags = flags;
}

// ---- disassembly ----------------------------------------------------------

uint8_t Debugger::disassemble(uint16_t addr, const DebugBank &bank, char *out, uint16_t outSize)
{
  uint8_t bytes[3];
  peekRange(addr, bytes, 3, bank);
  return dbgDis.instructionToOperands(addr, bytes, out, outSize);
}

// ---- trace ----------------------------------------------------------------

void Debugger::setTrace(bool on)
{
  if (on && !traceOn) {
    traceHead = 0;
    traceFill = 0;
  }
  traceOn = on;
  rearm();
}

uint32_t Debugger::traceCount()
{
  return traceFill;
}

uint16_t Debugger::traceEntry(uint32_t back)
{
  if (back >= traceFill) return 0;
  // traceHead is the slot after the newest entry.
  uint32_t idx = (traceHead + DBG_TRACE_DEPTH - 1 - back) % DBG_TRACE_DEPTH;
  return trace[idx];
}

// ---- machine state --------------------------------------------------------

uint16_t Debugger::softSwitches()
{
  AppleMMU *mmu = theMMU();
  return mmu ? mmu->videoSwitches() : 0;
}

// Fold an Apple screen code (normal / inverse / flashing / lowercase) down
// to a printable ASCII byte.
static char foldScreenChar(uint8_t c)
{
  c &= 0x7F;
  if (c < 0x20) c += 0x40;
  return (char)c;
}

// The first byte of visible text row 'row' (0..23). Apple II text rows are
// interleaved: base + 0x80*(row&7) + 0x28*(row>>3).
static uint16_t textRowBase(uint16_t base, uint8_t row)
{
  return base + 0x80 * (row & 7) + 0x28 * (row >> 3);
}

uint8_t Debugger::textScreen(char out[24][80], uint16_t base)
{
  static const DebugBank mainBank = { DBG_BANK_MAIN, 0, 1 };
  static const DebugBank auxBank  = { DBG_BANK_AUX, 0, 1 };

  memset(out, 0, 24 * 80);

  uint16_t sw = softSwitches();
  if (base == 0 && (sw & S_80COL)) {
    // 80-column text always lives on page 1 ($400): aux RAM holds the even
    // (left) column of each cell and main RAM the odd (right) column.
    for (uint8_t row = 0; row < 24; row++) {
      uint16_t rowBase = textRowBase(0x400, row);
      for (uint8_t i = 0; i < 40; i++) {
	out[row][i*2]     = foldScreenChar(peek(rowBase + i, auxBank));
	out[row][i*2 + 1] = foldScreenChar(peek(rowBase + i, mainBank));
      }
    }
    return 80;
  }

  if (base == 0) {
    // 80STORE on pins the scanner to page 1; PAGE2 is then only a
    // main/aux steering bit for the CPU's $0400-$07FF accesses.
    base = ((sw & S_PAGE2) && !(sw & S_80STORE)) ? 0x800 : 0x400;
  }
  for (uint8_t row = 0; row < 24; row++) {
    uint16_t rowBase = textRowBase(base, row);
    for (uint8_t col = 0; col < 40; col++) {
      out[row][col] = foldScreenChar(peek(rowBase + col, mainBank));
    }
  }
  return 40;
}

// ---- keyboard injection ---------------------------------------------------

static uint8_t hexNibble(char x)
{
  if (x >= '0' && x <= '9') return x - '0';
  if (x >= 'a' && x <= 'f') return x - 'a' + 10;
  if (x >= 'A' && x <= 'F') return x - 'A' + 10;
  return 0;
}

uint16_t Debugger::injectKeys(const char *s)
{
  if (!g_vm) return 0;
  AppleKeyboard *kbd = (AppleKeyboard *)g_vm->getKeyboard();
  uint16_t queued = 0;
  const char *p = s;
  while (*p) {
    uint8_t c;
    if (*p == '\\' && *(p+1)) {
      p++;
      switch (*p) {
      case 'r': case 'n': c = 0x0D; break;
      case 't':           c = 0x09; break;
      case 'e':           c = 0x1B; break;
      case '0':           c = 0x00; break;
      case '\\':          c = 0x5C; break;
      case 'x':
	if (*(p+1) && *(p+2)) {
	  c = (hexNibble(*(p+1)) << 4) | hexNibble(*(p+2));
	  p += 2;
	} else {
	  c = 'x';
	}
	break;
      default:            c = (uint8_t)*p; break;
      }
      p++;
    } else {
      c = (uint8_t)*p++;
    }
    if (!kbd->injectByte(c)) break;
    queued++;
  }
  return queued;
}

uint16_t Debugger::injectQueueDepth()
{
  if (!g_vm) return 0;
  return ((AppleKeyboard *)g_vm->getKeyboard())->injectQueueDepth();
}

#endif // AIIE_DEBUGGER
