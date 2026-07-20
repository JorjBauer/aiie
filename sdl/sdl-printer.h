#ifndef __SDL_PRINTER_H
#define __SDL_PRINTER_H

#include <stdlib.h>
#include <stdint.h>

#include <SDL.h>
#include <SDL_mutex.h>
#include <SDL_events.h>

#include "physicalprinter.h"

#define HEIGHT 800        // window height == the viewport into the paper roll
#define NATIVEWIDTH 960   // FIXME: printer can change density...
#define WIDTH 960

// The printer output is a continuously growing "paper roll": each page is
// appended below the previous one instead of overwriting it. The window is a
// viewport that follows the newest output as printing continues; the mouse wheel
// and arrows scroll back through earlier pages, S saves the roll to per-page PNGs
// (then clears it for the next job), and C clears without saving.
class SDLPrinter : public PhysicalPrinter {
 public:
  SDLPrinter();
  virtual ~SDLPrinter();

  virtual void addLine(uint8_t *rowOfBits); // 960 pixels wide (120 bytes) x 9 rows
  virtual void update();
  virtual void moveDownPixels(uint8_t p);

  void savePng();                           // saves the roll to the next free page-N.png files
  void clear();                             // blank the roll and start over
  void scrollByRows(int deltaRows);         // wheel review: +down (newer) / -up (older)
  uint32_t windowID();                      // 0 until the window is created

  // When the roll fills to the page cap the printer halts and shows a prompt;
  // the main loop freezes the VM while this is true (S saves, C clears/resumes).
  bool isHalted() { return halted; }
  bool isFocused();                         // true when the printer window has key focus
  void raiseWindow();                       // bring the printer window to the front
  void focusWindow();                       // raise it AND take keyboard focus (on click)

 private:
  void ensureRows(uint32_t need);           // grow the roll to hold at least `need` rows
  void writePngPage(const char *path, uint32_t startRow, uint32_t nrows); // one page image

  bool isDirty;
  bool halted;            // roll full: VM paused, waiting for save/clear
  bool haltShown;         // have we already raised the window for the current halt?
  uint32_t saveToastFrames; // frames left to show the "saved" confirmation
  uint32_t saveToastPages;  // page count for that confirmation
  char     saveToastMsg[96];// filename range for that confirmation
  uint32_t ypos;          // print-head row within the roll (absolute)
  uint32_t contentRows;   // furthest row drawn so far
  uint32_t allocRows;     // rows currently allocated in _bitmap
  uint32_t scrollY;       // top row of the visible viewport
  bool follow;            // auto-scroll to keep the newest output in view

  uint8_t  *_bitmap;      // WIDTH * allocRows, grows with the roll
  uint32_t *_viewPixels;  // WIDTH * HEIGHT ARGB, the viewport uploaded to the texture

  SDL_Window   *window;
  SDL_Renderer *renderer;
  SDL_Texture  *texture;
  SDL_mutex    *printerMutex;
};

#endif
