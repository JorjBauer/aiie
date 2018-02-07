#include "linux-keyboard.h"

#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <linux/input.h>

#include "sdl-paddles.h"
#include "globals.h"

#include "physicalkeyboard.h"

LinuxKeyboard::LinuxKeyboard(VMKeyboard *k) : PhysicalKeyboard(k)
{
  fd = open("/dev/input/by-path/platform-20980000.usb-usb-0:1:1.0-event-kbd",
	    O_RDONLY | O_NONBLOCK);
  
}

LinuxKeyboard::~LinuxKeyboard()
{
  close(fd);
}

// FIXME: dummy value
#define BIOSKEY 254

static uint8_t keymap[] = {
  0, // keycode 0 doesn't exist
  PK_ESC,
  '1',
  '2',
  '3',
  '4',
  '5',
  '6',
  '7',
  '8',
  '9',
  '0',
  '-',
  '=',
  PK_DEL,
  PK_TAB,
  'q',
  'w',
  'e',
  'r',
  't',
  'y',
  'u',
  'i',
  'o',
  'p',
  '[',
  ']',
  13,
  PK_CTRL,
  'a',
  's',
  'd',
  'f',
  'g',
  'h',
  'j',
  'k',
  'l',
  ';',
  '\'',
  '`',
  PK_LSHFT,
  '\\',
  'z',
  'x',
  'c',
  'v',
  'b',
  'n',
  'm',
  ',',
  '.',
  '/',
  PK_RSHFT,
  '*',
  PK_LA,
  ' ',
  PK_LOCK,
  0, // F1,
  0, // F2,
  0, // F3,
  0, // F4,
  0, // F5,
  0, // F6,
  0, // F7,
  0, // F8,
  0, // F9,
  0, // F10,
  0, // numlock
  0, // scrolllock
  0, // HOME7
  0, // UP8
  0, // PGUP 9
  0,
  0, // LEFT4
  '5', // number pad 5?
  0, // RTARROW6
  '+',
  0, // END1
  0, // DOWN2
  0, // PGDN3
  0, // INS
  0, // DEL
  0,
  0,
  0,
  0, // F11
  BIOSKEY, // F12
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  PK_RET, // number pad enter?
  PK_CTRL, // Right control?
  '/',
  0, // prtscr
  PK_RA,
  0,
  0, // HOME
  PK_UARR,
  0, // PGUP
  PK_LARR,
  PK_RARR,
  0, // END
  PK_DARR,
  0, // PGDN
  0, // INSERT
  PK_DEL,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0 // PAUSE
};
  

static uint8_t mapkeycode(uint16_t v)
{
  if (v < sizeof(keymap))
    return keymap[v];
  else
    return 0;
}

void LinuxKeyboard::maintainKeyboard()
{
  struct input_event ev;
  ssize_t n;
  
  n = ::read(fd, &ev, sizeof ev);
  if (n == sizeof(ev)) {
    if (ev.type == EV_KEY && ev.value >= 0 && ev.value <= 2) {
      uint8_t code = mapkeycode(ev.code);
      if (code == BIOSKEY) {
	g_biosInterrupt = true;
	return;
      }
      
      if (code) {
	switch (ev.value) {
	case 0: // release
	  vmkeyboard->keyReleased(code);
	  break;
	case 1: // press
	  vmkeyboard->keyDepressed(code);
	  break;
	case 2: // autorepeat
	  break;
	}
      }
    }
  }
}

bool keyHitPending = false;
uint8_t keyPending;

bool LinuxKeyboard::kbhit()
{
  struct input_event ev;
  ssize_t n;

  n = ::read(fd, &ev, sizeof ev);
  if (n == sizeof(ev)) {
    if (ev.type == EV_KEY && ev.value == 1) {
      uint8_t code = mapkeycode(ev.code);
      if (code && code != BIOSKEY) {
	keyHitPending = true;
	keyPending = code;
      }
    }
  }
  return keyHitPending;
}

int8_t LinuxKeyboard::read()
{
  if (keyHitPending) {
    keyHitPending = false;
    return keyPending;
  }
  return 0;
}

