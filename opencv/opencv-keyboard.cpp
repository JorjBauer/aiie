#include "opencv-keyboard.h"

/* 
 * OpenCV doesn't support very sophisticated keyboard interaction.
 * You can't query modifier keys; you don't get up-and-down
 * events. There's just a simple "has any key been pressed" method.
 */

#include "opencv2/core/core.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/calib3d/calib3d.hpp"
#include "opencv2/features2d/features2d.hpp"

using namespace cv;
using namespace std;

OpenCVKeyboard::OpenCVKeyboard(VMKeyboard *k) : PhysicalKeyboard(k)
{
}

OpenCVKeyboard::~OpenCVKeyboard()
{
}

typedef struct {
  int8_t actualKey;
  bool shifted;
} keymapChar;

// keymap starts at space (32), ends at delete (127).
// Note that this maps them based on what the Apple //e expects. It's the 
// same as a modern US keyboard.
const keymapChar keymap[96] = 
  { { ' ', false }, // space
    { '1', true  }, // !
    { '\'', true }, // "
    { '3', true  }, // #
    { '4', true  }, // $
    { '5', true  }, // %
    { '7', true  }, // &
    { '\'', false}, // '
    { '9', true  }, // (
    { '0', true  }, // )
    { '8', true  }, // *
    { '=', true  }, // +
    { ',', false }, // ,
    { '-', false }, // -
    { '.', false }, // .
    { '/', false }, // /
    { '0', false }, // 0
    { '1', false }, // 1
    { '2', false }, // 2
    { '3', false }, // 3
    { '4', false }, // 4
    { '5', false }, // 5
    { '6', false }, // 6
    { '7', false }, // 7
    { '8', false }, // 8
    { '9', false }, // 9
    { ':', true  }, // :
    { ';', false }, // ;
    { ',', true  }, // <
    { '=', false }, // =
    { '.', true  }, // >
    { '/', true  }, // ?
    { '2', true  }, // @
    { 'A', true  }, // A
    { 'B', true  }, // B
    { 'C', true  }, // C
    { 'D', true  }, // D
    { 'E', true  }, // E
    { 'F', true  }, // F
    { 'G', true  }, // G
    { 'H', true  }, // H
    { 'I', true  }, // I
    { 'J', true  }, // J
    { 'K', true  }, // K
    { 'L', true  }, // L
    { 'M', true  }, // M
    { 'N', true  }, // N
    { 'O', true  }, // O
    { 'P', true  }, // P
    { 'Q', true  }, // Q
    { 'R', true  }, // R
    { 'S', true  }, // S
    { 'T', true  }, // T
    { 'U', true  }, // U
    { 'V', true  }, // V
    { 'W', true  }, // W
    { 'X', true  }, // X
    { 'Y', true  }, // Y
    { 'Z', true  }, // Z
#ifndef TEENSYDUINO
    { LA, false }, // [
#else
    { '[', false }, // [
#endif
    { '\\', false}, // \  ...
#ifndef TEENSYDUINO
    { RA, false }, // ]
#else
    { ']', false }, // ]
#endif
    { '6', true  }, // ^
    { '-', true  }, // _
    { '`', false }, // `
    { 'A', false }, // a
    { 'B', false }, // b
    { 'C', false }, // c
    { 'D', false }, // d
    { 'E', false }, // e
    { 'F', false }, // f
    { 'G', false }, // g
    { 'H', false }, // h
    { 'I', false }, // i
    { 'J', false }, // j
    { 'K', false }, // k
    { 'L', false }, // l
    { 'M', false }, // m
    { 'N', false }, // n
    { 'O', false }, // o
    { 'P', false }, // p
    { 'Q', false }, // q
    { 'R', false }, // r
    { 'S', false }, // s
    { 'T', false }, // t
    { 'U', false }, // u
    { 'V', false }, // v
    { 'W', false }, // w
    { 'X', false }, // x
    { 'Y', false }, // y
    { 'Z', false }, // z
    { '[', true  }, // {
    { '\\', true }, // |
    { ']', true  }, // }
    { '`', true  }, // ~
    { DEL, false }  // delete
  };

void OpenCVKeyboard::maintainKeyboard()
{
  static int invalidCount = 0;
  int c = cv::waitKey(1);
  // OpenCV relies on KeyRepeat to deliver individual presses. We want 
  // to guess about "it's still down": so, unless we get > 3 invalid 
  // "-1" values in a row, we're gonna assume it's still pressed.
  if (c == -1) {
    invalidCount++;
    if (invalidCount < 4) {
      return;
    }
  } else {
    invalidCount = 0;
  }

  if (c == -1) {
    if (lastKey >= ' ' && lastKey <= 127) {
      vmkeyboard->keyReleased(keymap[lastKey-' '].actualKey);
      if (keymap[lastKey-' '].shifted) {
	vmkeyboard->keyReleased(LSHFT);
      }
    } else {
      vmkeyboard->keyReleased(lastKey);
    }

    lastKey = -1;
    return;
  }


  if (c == 0xF700) {
    c = UARR; // up
  } else if (c == 0xF701) {
    c = DARR; // down
  } else if (c == 0xF702) {
    c = LARR; // left
  } else if (c == 0xF703) {
    c = RARR; // right
  }

  // If we're already repeating this key, do nothing
  if (lastKey != -1 &&
      lastKey == c) {
    return;
  }

  // If it's a different key, then do a key up
  if (lastKey != -1) {
    if (lastKey >= ' ' && lastKey <= 127) {
      vmkeyboard->keyReleased(keymap[lastKey-' '].actualKey);
      if (keymap[lastKey-' '].shifted) {
	vmkeyboard->keyReleased(LSHFT);
      }
    } else {
      vmkeyboard->keyReleased(lastKey);
    }

    lastKey = -1;
  }

  // Now it's a new keypress - do a key down

  if (c >= ' ' && c <= 127) {
    if (keymap[c-' '].shifted) {
      vmkeyboard->keyDepressed(LSHFT);
    }
    vmkeyboard->keyDepressed(keymap[c-' '].actualKey);

    lastKey = c;
    return;
  }
  
  // Any other key that could have been reported:
  // ESC RET TAB LA RA LARR RARR DARR UARR
  vmkeyboard->keyDepressed(c);
  
  // anything else isn't reported by OpenCV, so we can't report it
  // specifically. So these are ignored:
  // _CTRL LSHFT RSHFT LOCK LA RA
  
  lastKey = c;
}
