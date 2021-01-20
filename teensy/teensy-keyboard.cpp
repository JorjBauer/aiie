#include <Arduino.h>
#include "teensy-keyboard.h"
#include <Keypad.h>
#include "teensy-println.h"

#include "globals.h"
#include "teensy-mouse.h"

const byte ROWS = 5;
const byte COLS = 13;

char keys[ROWS][COLS] = {
  {  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', PK_DEL },
  {  PK_ESC, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']' },
  { PK_CTRL, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', PK_RET },
  { PK_LSHFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', PK_RSHFT, 0 },
  { PK_LOCK, '`', PK_TAB, '\\', PK_LA, ' ', PK_RA, PK_LARR, PK_RARR, PK_DARR, PK_UARR, 0, 0 }
};

uint8_t rowsPins[ROWS] = { 33, 34, 35, 36, 37 };
uint8_t colsPins[COLS] = { 41, 40, 3, 4, 24, 25, 39, 23, 28, 29, 30, 31, 32 }; // 0, 1, 26, 27 are moving to ... 41, 40, 39, 23?
Keypad keypad(makeKeymap(keys), rowsPins, colsPins, ROWS, COLS);

struct _tkb_event {
  uint8_t keycode;
  bool pressedIfTrue;
  struct _tkb_event *next;
};

#define MAXKBEVENTS 10
struct _tkb_event keyboardEvents[MAXKBEVENTS];
uint8_t kbEventCount = 0;
uint8_t kbEventHead = 0;
uint8_t kbEventPtr = 0;

bool addEvent(uint8_t kc, bool pressed)
{
  if (kbEventCount >= MAXKBEVENTS)
    return false;

  keyboardEvents[kbEventPtr].keycode = kc;
  keyboardEvents[kbEventPtr++].pressedIfTrue = pressed;
  if (kbEventPtr >= MAXKBEVENTS) kbEventPtr = 0;
  kbEventCount++;
}

bool popEvent(uint8_t *kc, bool *pressed)
{
  if (kbEventCount) {
    *kc = keyboardEvents[kbEventHead].keycode;
    *pressed = keyboardEvents[kbEventHead++].pressedIfTrue;
    if (kbEventHead >= MAXKBEVENTS) kbEventHead = 0;
    kbEventCount--;
    return true;
  }
  return false;
}

static uint8_t shiftedNumber[] = { '<', // ,
				   '_', // -
				   '>', // .
				   '?', // /
				   ')', // 0
				   '!', // 1
				   '@', // 2
				   '#', // 3
				   '$', // 4
				   '%', // 5
				   '^', // 6
				   '&', // 7
				   '*', // 8
				   '(', // 9
				   0,   // (: is not a key)
				   ':'  // ;
};

TeensyKeyboard::TeensyKeyboard(VMKeyboard *k) : PhysicalKeyboard(k)
{
  keypad.setDebounceTime(5);

  leftShiftPressed = false;
  rightShiftPressed = false;
  ctrlPressed = false;
  capsLock = true;
  leftApplePressed = false;
  rightApplePressed = false;

  numPressed = 0;
}

TeensyKeyboard::~TeensyKeyboard()
{
}

void TeensyKeyboard::pressedKey(uint8_t key)
{
  numPressed++;

  if (key & 0x80) {
    // it's a modifier key.
    switch (key) {
    case PK_CTRL:
      ctrlPressed = 1;
      break;
    case PK_LSHFT:
      leftShiftPressed = 1;
      break;
    case PK_RSHFT:
      rightShiftPressed = 1;
       break;
    case PK_LOCK:
      capsLock = !capsLock;
      break;
    case PK_LA:
      leftApplePressed = 1;
      break;
    case PK_RA:
      ((TeensyMouse *)g_mouse)->mouseButtonEvent(true);
      rightApplePressed = 1;
      break;
    }
    return;
  }

  if (key == ' ' || key == PK_DEL || key == PK_ESC || key == PK_RET || key == PK_TAB) {
    addEvent(key, true);
    return;
  }

  if (key >= 'a' &&
      key <= 'z') {
    if (ctrlPressed) {
      addEvent(key - 'a' + 1, true);
      return;
    }
    if (leftShiftPressed || rightShiftPressed || capsLock) {
      addEvent(key - 'a' + 'A', true);
      return;
    }
    addEvent(key, true);
    return;
  }

  // FIXME: can we control-shift?
  if (key >= ',' && key <= ';') {
    if (leftShiftPressed || rightShiftPressed) {
      addEvent(shiftedNumber[key - ','], true);
      return;
    }
    addEvent(key, true);
    return;
  }

  if (leftShiftPressed || rightShiftPressed) {
    uint8_t ret = 0;
    switch (key) {
    case '=':
      ret = '+';
      break;
    case '[':
      ret = '{';
      break;
    case ']':
      ret = '}';
      break;
    case '\\':
      ret = '|';
      break;
    case '\'':
      ret = '"';
      break;
    case '`':
      ret = '~';
      break;
    }
    if (ret) {
      addEvent(ret, true);
      return;
    }
  }

  // Everything else falls through.
  addEvent(key, true);
}

void TeensyKeyboard::releasedKey(uint8_t key)
{
  numPressed--;
  if (key & 0x80) {
    // it's a modifier key.
    switch (key) {
    case PK_CTRL:
      ctrlPressed = 0;
      break;
    case PK_LSHFT:
      leftShiftPressed = 0;
      break;
    case PK_RSHFT:
      rightShiftPressed = 0;
      break;
    case PK_LA:
      ((TeensyMouse *)g_mouse)->mouseButtonEvent(false);
      leftApplePressed = 0;
      break;
    case PK_RA:
      rightApplePressed = 0;
      break;
    }
  }
  addEvent(key, false);
}

bool TeensyKeyboard::kbhit()
{
  if (keypad.getKeys()) {
    for (int i=0; i<LIST_MAX; i++) {
      if ( keypad.key[i].stateChanged ) {
        switch (keypad.key[i].kstate) {
	case PRESSED:
	  pressedKey(keypad.key[i].kchar);
	  break;
	case RELEASED:
	  releasedKey(keypad.key[i].kchar);
	  break;
	case HOLD:
	case IDLE:
	  break;
	}
      }
    }
  }

  return kbEventCount;
}

int8_t TeensyKeyboard::read()
{
  if (kbEventCount) {
    uint8_t kc;
    bool pressed;
    if (popEvent(&kc, &pressed)) {
      if (pressed) {
	return kc;
      }
    }
  }

  return 0;
}

// This is the interface to the physical keyboard, as used by the VM.
void TeensyKeyboard::maintainKeyboard()
{
  kbhit();

  if (kbEventCount) {
    uint8_t kc;
    bool pressed;
    if (popEvent(&kc, &pressed)) {
      if (pressed) {
	sprintf(debugBuf, "%d press  ", kc);
	g_display->debugMsg(debugBuf);
	vmkeyboard->keyDepressed(kc);
      } else {
	sprintf(debugBuf, "%d relsd  ", kc);
	g_display->debugMsg(debugBuf);
	vmkeyboard->keyReleased(kc);
      }
    }
  }
}
