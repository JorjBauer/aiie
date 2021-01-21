#include "sdl-keyboard.h"

#include "sdl-paddles.h"
#include "sdl-mouse.h"
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

  // Paddles
  if (key->keysym.sym == SDLK_LGUI) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_LA);
    else
      vmkeyboard->keyDepressed(PK_LA);
  }

  if (key->keysym.sym == SDLK_RGUI) {
    if (releaseEvent)
      vmkeyboard->keyReleased(PK_RA);
    else
      vmkeyboard->keyDepressed(PK_RA);
  }
}



void SDLKeyboard::maintainKeyboard()
{
  SDL_Event event;
  if (SDL_PollEvent( &event )) {

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
      ((SDLMouse *)g_mouse)->mouseButtonEvent(event.type == SDL_MOUSEBUTTONDOWN);
      break;
    case SDL_MOUSEMOTION:
      // We are handling the SDL input loop, so need to pass this off to the paddles. :/
      // FIXME: nasty rooting around in other objects and typecasting.
      // FIXME: event.motion.state & SDL_BUTTON_LMASK, et al?

      ((SDLPaddles *)g_paddles)->gotMouseMovement(event.motion.x, event.motion.y);
      ((SDLMouse *)g_mouse)->gotMouseEvent(event.motion.state, // button
					   event.motion.xrel, event.motion.yrel);
      break;

      // FIXME: this really doesn't belong here. The mouse kinda
      // didn't belong here, but this REALLY is wrong.
    case SDL_WINDOWEVENT:
      if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
	printf("Window was resized\n");
	((SDLDisplay *)g_display)->windowResized(event.window.data1, event.window.data2);
      }
      break;

    case SDL_QUIT:
      exit(0);
    }
  }
}

bool hasKeyPending;
uint8_t keyPending;

bool SDLKeyboard::kbhit()
{
  SDL_Event event;
  if (SDL_PollEvent( &event )) {
    if (event.type == SDL_QUIT) {
      exit(0);
    }
    
    if (event.type == SDL_KEYDOWN) {
      SDL_KeyboardEvent *key = &event.key;
      
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
	keyPending = key->keysym.sym;
	hasKeyPending = true;
      } else {
	switch (key->keysym.sym) {
	case SDLK_UP:
	  keyPending = PK_UARR;
	  hasKeyPending = true;
	  break;
	case SDLK_DOWN:
	  keyPending = PK_DARR;
	  hasKeyPending = true;
	  break;
	case SDLK_RIGHT:
	  keyPending = PK_RARR;
	  hasKeyPending = true;
	  break;
	case SDLK_LEFT:
	  keyPending = PK_LARR;
	  hasKeyPending = true;
	  break;
	}
      }
    }
  }
  return hasKeyPending;
}

int8_t SDLKeyboard::read()
{
  // Meh
  hasKeyPending = false;
  return keyPending;
}


