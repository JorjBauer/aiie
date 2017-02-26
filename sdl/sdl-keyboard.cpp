#include "sdl-keyboard.h"

#include "sdl-paddles.h"
#include "globals.h"

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
      vmkeyboard->keyReleased(DEL);
    else
      vmkeyboard->keyDepressed(DEL);
    return;
  }

  //modifier handling
  if (key->keysym.sym == SDLK_CAPSLOCK) {
    if (releaseEvent)
      vmkeyboard->keyReleased(LOCK);
    else
      vmkeyboard->keyDepressed(LOCK);
  }

  if (key->keysym.sym == SDLK_LSHIFT ||
      key->keysym.sym == SDLK_RSHIFT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(LSHFT);
    else
      vmkeyboard->keyDepressed(LSHFT);
  }

  // arrows
  if (key->keysym.sym == SDLK_LEFT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(LARR);
    else
      vmkeyboard->keyDepressed(LARR);
  }
  if (key->keysym.sym == SDLK_RIGHT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(RARR);
    else
      vmkeyboard->keyDepressed(RARR);
  }

  if (key->keysym.sym == SDLK_LEFT) {
    if (releaseEvent)
      vmkeyboard->keyReleased(LARR);
    else
      vmkeyboard->keyDepressed(LARR);
  }

  if (key->keysym.sym == SDLK_UP) {
    if (releaseEvent)
      vmkeyboard->keyReleased(UARR);
    else
      vmkeyboard->keyDepressed(UARR);
  }
    
  if (key->keysym.sym == SDLK_DOWN) {
    if (releaseEvent)
      vmkeyboard->keyReleased(DARR);
    else
      vmkeyboard->keyDepressed(DARR);
  }

  // Paddles
  if (key->keysym.sym == SDLK_LGUI) {
    if (releaseEvent)
      vmkeyboard->keyReleased(LA);
    else
      vmkeyboard->keyDepressed(LA);
  }

  if (key->keysym.sym == SDLK_RGUI) {
    if (releaseEvent)
      vmkeyboard->keyReleased(RA);
    else
      vmkeyboard->keyDepressed(RA);
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
    case SDL_MOUSEMOTION:
      // We are handling the SDL input loop, so need to pass this off to the paddles. :/
      // FIXME: nasty rooting around in other objects and typecasting.
      // FIXME: event.motion.state & SDL_BUTTON_LMASK, et al?

      ((SDLPaddles *)g_paddles)->gotMouseMovement(event.motion.x, event.motion.y);
      break;

    case SDL_QUIT:
      exit(0);
    }
  }
}
