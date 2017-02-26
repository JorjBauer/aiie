#ifndef __SDL_KEYBOARD_H
#define __SDL_KEYBOARD_H

#include "physicalkeyboard.h"
#include "vmkeyboard.h"

#include <SDL2/SDL.h>

class SDLKeyboard : public PhysicalKeyboard {
 public:
  SDLKeyboard(VMKeyboard *k);
  virtual ~SDLKeyboard();
  
  virtual void maintainKeyboard();

 private:
  void handleKeypress(SDL_KeyboardEvent *key);
};

#endif
