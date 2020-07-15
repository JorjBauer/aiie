#include <Arduino.h>
#include "teensy-keyboard.h"
#include <Keypad.h>
#include "LRingBuffer.h"
#include "teensy-println.h"

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
LRingBuffer buffer(10); // 10 keys should be plenty, right?

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
      rightApplePressed = 1;
      break;
    }
    return;
  }

  if (key == ' ' || key == PK_DEL || key == PK_ESC || key == PK_RET || key == PK_TAB) {
    buffer.addByte(key);
    return;
  }

  if (key >= 'a' &&
      key <= 'z') {
    if (ctrlPressed) {
      buffer.addByte(key - 'a' + 1);
      return;
    }
    if (leftShiftPressed || rightShiftPressed || capsLock) {
      buffer.addByte(key - 'a' + 'A');
      return;
    }
    buffer.addByte(key);
    return;
  }

  // FIXME: can we control-shift?
  if (key >= ',' && key <= ';') {
    if (leftShiftPressed || rightShiftPressed) {
      buffer.addByte(shiftedNumber[key - ',']);
      return;
    }
    buffer.addByte(key);
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
      buffer.addByte(ret);
      return;
    }
  }

  // Everything else falls through.
  buffer.addByte(key);
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
      leftApplePressed = 0;
      break;
    case PK_RA:
      rightApplePressed = 0;
      break;
    }
  }
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

  // For debugging: also allow USB serial to act as a keyboard
  if (serialavailable()) {
    buffer.addByte(serialgetch());
  }

  return buffer.hasData();
}

int8_t TeensyKeyboard::read()
{
  if (buffer.hasData()) {
    return buffer.consumeByte();
  }

  return 0;
}

// This is a non-buffered interface to the physical keyboard, as used
// by the VM.
void TeensyKeyboard::maintainKeyboard()
{
  if (keypad.getKeys()) {
    for (int i=0; i<LIST_MAX; i++) {
      if ( keypad.key[i].stateChanged ) {
        switch (keypad.key[i].kstate) {
	case PRESSED:
	  vmkeyboard->keyDepressed(keypad.key[i].kchar);
	  break;
	case RELEASED:
	  vmkeyboard->keyReleased(keypad.key[i].kchar);
	  break;
	case HOLD:
	case IDLE:
	  break;
	}
      }
    }
  }

  // For debugging: also allow USB serial to act as a keyboard
  if (serialavailable()) {
    int c = serialgetch();
    vmkeyboard->keyDepressed(c);
    vmkeyboard->keyReleased(c);
  }
}
