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
  { PK_LSHFT, 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', PK_RSHFT, PK_NONE },
  { PK_LOCK, '`', PK_TAB, '\\', PK_LA, ' ', PK_RA, PK_LARR, PK_RARR, PK_DARR, PK_UARR, PK_NONE, PK_NONE }
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

bool TeensyKeyboard::addEvent(uint8_t kc, bool pressed)
{
  if (kbEventCount >= MAXKBEVENTS)
    return false;

  if (pressed && kbEventCount+numPressed >= MAXKBEVENTS) {
    // save space in the event queue for any keyup events that may come
    return false;
  }

  keyboardEvents[kbEventPtr].keycode = kc;
  keyboardEvents[kbEventPtr++].pressedIfTrue = pressed;
  if (kbEventPtr >= MAXKBEVENTS) kbEventPtr = 0;
  kbEventCount++;
}

bool TeensyKeyboard::popEvent(uint8_t *kc, bool *pressed)
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
  // Need to set the rows to be pull-ups early, so the pullups have
  // time to settle -- otherwise we get a phantom set of keypresses
  // on startup for all the keys in the first column
  for (byte i=0; i<ROWS; i++) {
    pinMode(rowsPins[i], INPUT_PULLUP);
  }
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

// apply modifiers to keycode and return result
uint8_t TeensyKeyboard::modifyKeycode(uint8_t key)
{
  if (key == ' ' || key == PK_DEL || key == PK_ESC || key == PK_RET || key == PK_TAB) {
    return key;
  }

  if (key >= 'a' &&
      key <= 'z') {
    if (ctrlPressed) {
      return key - 'a' + 1;
    }
    if (leftShiftPressed || rightShiftPressed || capsLock) {
      return key - 'a' + 'A';
    }
    return key;
  }

  // FIXME: can we control-shift?
  if (key >= ',' && key <= ';') {
    if (leftShiftPressed || rightShiftPressed) {
      return shiftedNumber[key - ','];
    }
    return key;
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
      return ret;
    }
  }
  return key;
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
    addEvent(key, true);
    return;
  }

  addEvent(modifyKeycode(key), true);
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
  addEvent(modifyKeycode(key), false);
}

bool TeensyKeyboard::kbhit()
{
  if (keypad.getKeys()) {
    for (int i=0; i<LIST_MAX; i++) {
      if ( keypad.key[i].stateChanged && keypad.key[i].kchar != PK_NONE ) {
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
	vmkeyboard->keyDepressed(kc);
      } else {
	vmkeyboard->keyReleased(kc);
      }
    }
  }
}
