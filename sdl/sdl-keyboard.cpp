#include "sdl-keyboard.h"

#include "sdl-paddles.h"
#include "sdl-mouse.h"
#include "sdl-printer.h"
#include "globals.h"
#include "sdl-display.h"

SDLKeyboard::SDLKeyboard(VMKeyboard *k) : PhysicalKeyboard(k)
{
}

SDLKeyboard::~SDLKeyboard()
{
}

typedef struct {
  int8_t actualKey;
  bool shifted;
} keymapChar;

void SDLKeyboard::handleKeypress(SDL_KeyboardEvent *key)
{
  bool releaseEvent = key->type == SDL_KEYUP;

  if (key->type == SDL_KEYDOWN &&
      key->keysym.sym == SDLK_F10) {
    // Invoke BIOS
    g_biosInterrupt = true;
    return;
  }

  if (key->type == SDL_KEYDOWN &&
      key->keysym.sym == SDLK_F8) {
    // Panic: clear a stuck key repeat (a lost key-up leaves a key "held down").
    vmkeyboard->releaseAllKeys();
    return;
  }

  if (key->type == SDL_KEYDOWN &&
      key->keysym.sym == SDLK_F9) {
    // Global save shortcut (works even when the main emulator window has focus).
    ((SDLPrinter *)g_printer)->savePng();
    return;
  }

  // Printer controls, active when the roll is full (halted, VM paused) or when the
  // printer window has keyboard focus. Letters/arrows are used so macOS Mission
  // Control doesn't swallow them the way it does F11 ("Show Desktop"). While active,
  // all key events are consumed so nothing leaks into the paused/unfocused VM.
  {
    SDLPrinter *pr = (SDLPrinter *)g_printer;
    if (pr && (pr->isHalted() || pr->isFocused())) {
      if (key->type == SDL_KEYDOWN) {
        switch (key->keysym.sym) {
        case 's': case 'S': pr->savePng(); break;
        case 'c': case 'C': pr->clear();             printf("Cleared printer roll\n");             break;
        case SDLK_UP:       pr->scrollByRows(-40);            break;
        case SDLK_DOWN:     pr->scrollByRows(40);             break;
        case SDLK_PAGEUP:   pr->scrollByRows(-(HEIGHT - 80)); break;
        case SDLK_PAGEDOWN: pr->scrollByRows(HEIGHT - 80);    break;
        case SDLK_HOME:     pr->scrollByRows(-1000000);       break;
        case SDLK_END:      pr->scrollByRows(1000000);        break;
        default: break;
        }
      }
      return; // consume down+up so nothing reaches the VM while the printer is active
    }
  }

  if ( (key->keysym.sym >= 'a' && key->keysym.sym <= 'z') ||
       (key->keysym.sym >= '0' && key->keysym.sym <= '9') ||
       key->keysym.sym == '-' ||
       key->keysym.sym == '=' ||
       key->keysym.sym == '[' ||
       key->keysym.sym == '`' ||
       key->keysym.sym == ']' ||
       key->keysym.sym == '\\' ||
       key->keysym.sym == ';' ||
       key->keysym.sym == '\'' ||
       key->keysym.sym == ',' ||
       key->keysym.sym == '.' ||
       key->keysym.sym == '/' ||
       key->keysym.sym == ' ' ||
       key->keysym.sym == 27 || // ESC
       key->keysym.sym == 13 || // return
       key->keysym.sym == 9) { // tab

    // Simple keypresses
    if (key->keysym.mod & (KMOD_LCTRL|KMOD_RCTRL)) {
      key->keysym.sym -= ('a'-1);
    }

    if (releaseEvent)
      vmkeyboard->keyReleased(key->keysym.sym);
    else
      vmkeyboard->keyDepressed(key->keysym.sym);

    return;
  }

  // delete key
  if (key->keysym.sym == 8) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_DEL);
    else
      vmkeyboard->keyDepressed(PK_DEL);
    return;
  }

  //modifier handling
  if (key->keysym.sym == SDLK_CAPSLOCK) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_LOCK);
    else
      vmkeyboard->keyDepressed(PK_LOCK);
  }

  if (key->keysym.sym == SDLK_LSHIFT ||
      key->keysym.sym == SDLK_RSHIFT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_LSHFT);
    else
      vmkeyboard->keyDepressed(PK_LSHFT);
  }

  // arrows
  if (key->keysym.sym == SDLK_LEFT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_LARR);
    else
      vmkeyboard->keyDepressed(PK_LARR);
  }
  if (key->keysym.sym == SDLK_RIGHT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_RARR);
    else
      vmkeyboard->keyDepressed(PK_RARR);
  }

  if (key->keysym.sym == SDLK_LEFT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_LARR);
    else
      vmkeyboard->keyDepressed(PK_LARR);
  }

  if (key->keysym.sym == SDLK_UP) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_UARR);
    else
      vmkeyboard->keyDepressed(PK_UARR);
  }
    
  if (key->keysym.sym == SDLK_DOWN) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_DARR);
    else
      vmkeyboard->keyDepressed(PK_DARR);
  }

  // Paddles / open- and closed-apple. Alt (Option) is accepted alongside
  // GUI (Command) because the host OS hijacks many Command combinations.
  if (key->keysym.sym == SDLK_LGUI ||
      key->keysym.sym == SDLK_LALT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_LA);
    else
      vmkeyboard->keyDepressed(PK_LA);
  }

  if (key->keysym.sym == SDLK_RGUI ||
      key->keysym.sym == SDLK_RALT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_RA);
    else
      vmkeyboard->keyDepressed(PK_RA);
  }
}



// Pop a native modal confirmation before actually quitting. Cmd-Q, Cmd-W, and the
// window close button all reach us as a single SDL_QUIT, and quitting instantly is
// easy to trigger by accident (Cmd-Q sits right next to Cmd-W / Cmd-Tab) and drops
// the emulator with any in-flight disk writes unsaved. Returns true only when the
// user explicitly chooses to quit. On any failure to show the dialog we fall back to
// quitting, so a broken dialog can never trap the user in an un-closable window.
static bool confirmQuit()
{
  SDL_Window *parent = g_display ? ((SDLDisplay *)g_display)->getWindow() : NULL;

  const SDL_MessageBoxButtonData buttons[] = {
    { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" },
    { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Quit"   },
  };
  const SDL_MessageBoxData mbd = {
    SDL_MESSAGEBOX_WARNING,
    parent,
    "Quit Aiie?",
    "Quit the emulator? Any unsaved disk changes will be lost.",
    SDL_arraysize(buttons),
    buttons,
    NULL   // default (OS) color scheme
  };

  int buttonid = -1;
  if (SDL_ShowMessageBox(&mbd, &buttonid) < 0)
    return true;   // couldn't show the dialog; honor the quit rather than trap

  return buttonid == 1;
}

void SDLKeyboard::maintainKeyboard()
{
  // Drain the ENTIRE event queue each call, not one event per 60Hz tick. At high
  // emulation speed the main loop iterates slowly (more work per pass), so a
  // one-event-per-tick drain falls behind: a key-up can sit in the queue long
  // enough (~0.68s) to cross the auto-repeat threshold, which is exactly the
  // "Return sticks at 8x" symptom. Clearing the whole queue keeps key-ups prompt.
  SDL_Event event;
  while (SDL_PollEvent( &event )) {

    // Handle keydown/keyup (and quit, incidentally)
    switch (event.type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
      // Don't handle repeats; we have our own repeat code
      if (event.key.repeat == 0)
	handleKeypress(&event.key);
      break;
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
      // A click in the printer window claims keyboard focus for it (macOS won't
      // reliably do this for a secondary window on its own) and must not reach
      // the emulated Apple mouse.
      if (g_printer &&
          event.button.windowID == ((SDLPrinter *)g_printer)->windowID()) {
        if (event.type == SDL_MOUSEBUTTONDOWN)
          ((SDLPrinter *)g_printer)->focusWindow();
        break;
      }
      ((SDLMouse *)g_mouse)->mouseButtonEvent(event.type == SDL_MOUSEBUTTONDOWN);
      break;
    case SDL_MOUSEMOTION:
      // We are handling the SDL input loop, so need to pass this off to the paddles. :/
      // FIXME: nasty rooting around in other objects and typecasting.
      // FIXME: event.motion.state & SDL_BUTTON_LMASK, et al?

      // Motion over the printer window shouldn't jiggle the emulated paddles/mouse.
      if (g_printer &&
          event.motion.windowID == ((SDLPrinter *)g_printer)->windowID())
        break;

      ((SDLPaddles *)g_paddles)->gotMouseMovement(event.motion.x, event.motion.y);
      ((SDLMouse *)g_mouse)->gotMouseEvent(event.motion.state, // button
					   event.motion.xrel, event.motion.yrel);
      break;

    case SDL_MOUSEWHEEL:
      // Scroll the printer roll when the wheel is over the printer window.
      // wheel.y > 0 is "away/up", which should reveal earlier pages.
      if (g_printer &&
          event.wheel.windowID == ((SDLPrinter *)g_printer)->windowID()) {
        ((SDLPrinter *)g_printer)->scrollByRows(-event.wheel.y * 48);
      }
      break;

    case SDL_WINDOWEVENT:
      // On any focus transition, drop all held keys. A key whose key-up lands in
      // another window (or is swallowed by the OS during the switch) would
      // otherwise repeat forever; this makes returning to the emulator -- or just
      // clicking away and back -- always start from a clean keyboard.
      if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
          event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        vmkeyboard->releaseAllKeys();
      }
      break;

    case SDL_QUIT:
      if (confirmQuit())
	exit(0);
      break;
    }
  }
}

// A small FIFO of decoded keys for the BIOS. kbhit() drains the ENTIRE SDL event
// queue on each call and pushes any decoded keys here; read() pops one. The old
// code pulled a single SDL event per call, so a backlog of mouse-motion or
// key-up events sat in front of your keystrokes and the ~30Hz BIOS poll only
// cleared one event per tick -- which is exactly why typing an SSID or password
// felt halting and unnatural. Draining the queue every call means a keystroke is
// never stuck behind unrelated events.
#define BIOS_KEYRING 32
static uint8_t keyRing[BIOS_KEYRING];
static uint8_t keyRingHead = 0, keyRingTail = 0;

static void pushBiosKey(uint8_t k)
{
  uint8_t next = (uint8_t)((keyRingHead + 1) % BIOS_KEYRING);
  if (next != keyRingTail) {   // silently drop if the ring is somehow full
    keyRing[keyRingHead] = k;
    keyRingHead = next;
  }
}

bool SDLKeyboard::kbhit()
{
  SDL_Event event;
  while (SDL_PollEvent( &event )) {
    if (event.type == SDL_QUIT) {
      if (confirmQuit())
	exit(0);
      continue;
    }

    if (event.type == SDL_MOUSEMOTION) {
      // We are handling the SDL input loop, so need to pass this off to the paddles. :/
      // FIXME: nasty rooting around in other objects and typecasting.
      // FIXME: event.motion.state & SDL_BUTTON_LMASK, et al?

      ((SDLPaddles *)g_paddles)->gotMouseMovement(event.motion.x, event.motion.y);
      ((SDLMouse *)g_mouse)->gotMouseEvent(event.motion.state, // button
					   event.motion.xrel, event.motion.yrel);

    } else if (event.type == SDL_KEYDOWN) {
      SDL_KeyboardEvent *key = &event.key;
      // Keep the full SDL_Keycode: special keys (arrows, etc.) are large values
      // with the scancode bit set, and truncating to a narrow type mangles them
      // so their cases below never match.
      SDL_Keycode sym = key->keysym.sym;
      bool shift = (key->keysym.mod & KMOD_SHIFT) != 0;

      if ( (sym >= 'a' && sym <= 'z') ||
	   (sym >= '0' && sym <= '9') ||
	   sym == '-' ||
	   sym == '=' ||
	   sym == '[' ||
	   sym == '`' ||
	   sym == ']' ||
	   sym == '\\' ||
	   sym == ';' ||
	   sym == '\'' ||
	   sym == ',' ||
	   sym == '.' ||
	   sym == '/' ||
	   sym == ' ' ||
	   sym == 27 || // ESC
	   sym == 13 || // return
	   sym == 9) { // tab
	// SDL reports the physical (unshifted, lowercase) key. Shift a letter up
	// to uppercase so mixed-case SSIDs and passwords can actually be typed.
	if (shift && sym >= 'a' && sym <= 'z')
	  pushBiosKey((uint8_t)(sym - ('a' - 'A')));
	else
	  pushBiosKey((uint8_t)sym);
      } else {
	switch (sym) {
	case SDLK_UP:    pushBiosKey(PK_UARR); break;
	case SDLK_DOWN:  pushBiosKey(PK_DARR); break;
	case SDLK_RIGHT: pushBiosKey(PK_RARR); break;
	case SDLK_LEFT:  pushBiosKey(PK_LARR); break;
	case SDLK_BACKSPACE:  // delete key: backspace in the BIOS text fields
	case SDLK_DELETE:
	  pushBiosKey(PK_DEL);
	  break;
	}
      }
    }
  }
  return (keyRingHead != keyRingTail);
}

int8_t SDLKeyboard::read()
{
  if (keyRingHead == keyRingTail)
    return PK_NONE;
  uint8_t k = keyRing[keyRingTail];
  keyRingTail = (uint8_t)((keyRingTail + 1) % BIOS_KEYRING);
  return (int8_t)k;
}


