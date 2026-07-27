#include "applekeyboard.h"
#include "physicalkeyboard.h" // for LA/RA constants

#include "applemmu.h"

#include "globals.h"

// How many CPU cycles before we begin repeating a key?
#define STARTREPEAT 700000
// How many CPU cycles between repeats of a key?
#define REPEATAGAIN 66667

// The repeat constants above are calibrated for 1.023MHz. When the
// CPU runs faster or slower than that, scale them so key repeat
// stays constant in wall-clock time.
static int64_t scaleToSpeed(int64_t cycles)
{
  return (cycles * (int64_t)g_speed) / 1023000;
}

AppleKeyboard::AppleKeyboard(AppleMMU *m)
{
  this->mmu = m;

  for (uint16_t i=0; i<sizeof(keysDown); i++) {
    keysDown[i] = false;
  }
  anyKeyIsDown = false;
  startRepeatTimer = 0;
  repeatTimer = 0;

  capsLockEnabled = true;

  injectHead = injectTail = 0;
}

AppleKeyboard::~AppleKeyboard()
{
}

bool AppleKeyboard::isVirtualKey(uint8_t kc)
{
  if (kc >= 0x81 && kc <= 0x86) {
    return true;
  }
  return false;
}

// apply the apple keymap.
// FIXME: easier with an array, but is that better?
uint8_t AppleKeyboard::translateKeyWithModifiers(uint8_t k)
{
  // tolower, so we know what we're working with...
  if (k >= 'A' && k <= 'Z') {
    k = k - 'A' + 'a';
  }

  if (keysDown[PK_CTRL]) {
    if (k >= 'a' && k <= 'z') {
      return k - 'a' + 1;
    }
    // FIXME: any other control keys honored on the //e keyboard?
  }

  if (capsLockEnabled && k >= 'a' && k <= 'z') {
    return k - 'a' + 'A';
  }

  if (keysDown[PK_LSHFT] || keysDown[PK_RSHFT]) {
    if (k >= 'a' && k <= 'z') {
      return k - 'a' + 'A';
    }
    switch (k) {
    case '1':
      return '!';
    case '2':
      return '@';
    case '3':
      return '#';
    case '4':
      return '$';
    case '5':
      return '%';
    case '6':
      return '^';
    case '7':
      return '&';
    case '8':
      return '*';
    case '9':
      return '(';
    case '0':
      return ')';
    case '-':
      return '_';
    case '=':
      return '+';
    case '[':
      return '{';
    case ']':
      return '}';
    case '\\':
      return '|';
    case '`':
      return '~';
    case ';':
      return ':';
    case '\'':
      return '"';
    case ',':
      return '<';
    case '.':
      return '>';
    case '/':
      return '?';
    }
    // FIXME: what the heck is it? I guess we don't need to shift it?
  }

  // And if we fall through, then just return it as-is
  return k;
}

void AppleKeyboard::keyDepressed(uint8_t k)
{
  if (k >= 'A' && k <= 'Z') {
    k = k - 'A' + 'a';
  }
  
  keysDown[k] = true;  

  // If it's not a virtual key, then set the anyKeyDown flag
  // (the VM will see this as a keyboard key)
  if (!isVirtualKey(k)) {
    if (!anyKeyIsDown) {
      mmu->setKeyDown(true);
      anyKeyIsDown = true;
    }
    keyThatIsRepeating = translateKeyWithModifiers(k);
    startRepeatTimer = g_cpu->cycles + scaleToSpeed(STARTREPEAT);
    mmu->keyboardInput(keyThatIsRepeating);
  } else if (k == PK_LA) {
    // Special handling: apple keys
    mmu->setAppleKey(0, true);
    return;
  } else if (k == PK_RA) {
    // Special handling: apple keys
    mmu->setAppleKey(1, true);
    return;
  } else if (k == PK_LOCK) {
    // Special handling: caps lock
    capsLockEnabled = !capsLockEnabled;
    return;
  }
}

void AppleKeyboard::keyReleased(uint8_t k)
{
  if (k >= 'A' && k <= 'Z') {
    k = k - 'A' + 'a';
  }
  
  keysDown[k] = false;  

  // Special handling: apple keys
  if (k == PK_LA) {
    mmu->setAppleKey(0, false);
    return;
  }
  if (k == PK_RA) {
    mmu->setAppleKey(1, false);
    return;
  }
  if (k == PK_LOCK) {
    // Nothing to do when the caps lock key is released.
    return;
  }

  if (anyKeyIsDown) {
    anyKeyIsDown = false;
    for (size_t i=0; i<sizeof(keysDown); i++) {
      if (keysDown[i] && !isVirtualKey(i)) {
	anyKeyIsDown = true;
	break;
      }
    }
    if (!anyKeyIsDown) {
      mmu->setKeyDown(false);
    }
  }  
}

// Force every key up and stop any repeat in progress. Recovers from a stuck key
// when a key-up went missing (focus change, or the host OS swallowing the release
// of a key held with a modifier), which would otherwise repeat forever.
void AppleKeyboard::releaseAllKeys()
{
  for (size_t i=0; i<sizeof(keysDown); i++) {
    keysDown[i] = false;
  }
  anyKeyIsDown = false;
  startRepeatTimer = 0;
  repeatTimer = 0;
  mmu->setKeyDown(false);
  mmu->setAppleKey(0, false); // open apple
  mmu->setAppleKey(1, false); // closed apple
  // capsLockEnabled is a latched toggle, not a held key; leave it as-is.
}

bool AppleKeyboard::injectByte(uint8_t c)
{
  uint16_t next = (injectHead + 1) % kInjectQueueSize;
  if (next == injectTail) {
    return false; // queue full
  }
  injectQueue[injectHead] = c & 0x7F;
  injectHead = next;
  return true;
}

uint16_t AppleKeyboard::injectString(const char *s, uint16_t len)
{
  uint16_t queued = 0;
  for (uint16_t i=0; i<len; i++) {
    if (!injectByte((uint8_t)s[i])) {
      break;
    }
    queued++;
  }
  return queued;
}

uint16_t AppleKeyboard::injectQueueDepth()
{
  return (injectHead - injectTail + kInjectQueueSize) % kInjectQueueSize;
}

void AppleKeyboard::maintainKeyboard(int64_t cycleCount)
{
  // Scripted/automation type-ahead: deliver the next queued key only once the
  // running program has consumed the previous strobe (and no physical key is
  // held), so nothing is dropped or doubled however fast the CPU polls. This
  // takes priority over key-repeat, which can't be active while the queue
  // drains because a queued key is a momentary tap (anyKeyIsDown stays false).
  if (injectHead != injectTail &&
      !anyKeyIsDown &&
      !mmu->keyboardStrobePending()) {
    uint8_t c = injectQueue[injectTail];
    injectTail = (injectTail + 1) % kInjectQueueSize;
    mmu->injectKeypress(c);
    return;
  }

  if (anyKeyIsDown) {
    if (startRepeatTimer) {
      if (cycleCount >= startRepeatTimer) {
	// waiting to start repeating
	startRepeatTimer = 0;
	repeatTimer = 0;
	// Will fall through...
      } else {
	// Don't fall through; not time to start repeating yet
	return;
      }
    }
    
    // already repeating; keep it up
    if (cycleCount >= repeatTimer) {
      mmu->keyboardInput(keyThatIsRepeating);
      repeatTimer = cycleCount + scaleToSpeed(REPEATAGAIN);
    }
  }
}
