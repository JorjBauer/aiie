#include "sdl-printer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zlib.h>   // deflate + crc32 for the PNG writer
#include "font.h"   // asciiToAppleGlyph() for the "roll full" prompt text

#define WINDOWNAME "printer  [wheel/arrows scroll · S save · C clear]"

// How much the roll grows each time it runs out of room: two pages, so a long
// print job reallocates rarely.
#define GROW_ROWS (HEIGHT * 2)

// Memory guard. The roll is 1 byte/pixel, so a page is WIDTH*HEIGHT = 768 KB;
// 100 pages ~ 77 MB. When the roll reaches MAX_PAGES the printer halts (pausing
// the VM) and asks to be saved/cleared. One extra page of slack is allocated so
// the line in flight when the cap is hit is never clipped.
#define MAX_PAGES  100
#define LIMIT_ROWS ((uint32_t)MAX_PAGES * HEIGHT)
#define CAP_ROWS   ((uint32_t)(MAX_PAGES + 1) * HEIGHT)

// Stamp one glyph from the built-in 8x8 font into the ARGB viewport at (px,py),
// magnified by `scale`. Used only to draw the halt prompt over the printout.
static void stampChar(uint32_t *buf, int px, int py, char c, uint32_t color, int scale)
{
  const unsigned char *g = asciiToAppleGlyph((unsigned char)c);
  for (int row = 0; row < 8; row++) {
    unsigned char bitsrow = g[row];
    for (int col = 0; col < 8; col++) {
      if (!(bitsrow & (1 << col)))
        continue;
      for (int sy = 0; sy < scale; sy++)
        for (int sx = 0; sx < scale; sx++) {
          int X = px + col * scale + sx, Y = py + row * scale + sy;
          if (X >= 0 && X < WIDTH && Y >= 0 && Y < HEIGHT)
            buf[(size_t)Y * WIDTH + X] = color;
        }
    }
  }
}

static void stampText(uint32_t *buf, int px, int py, const char *s, uint32_t color, int scale)
{
  for (int x = px; *s; s++, x += 8 * scale)
    stampChar(buf, x, py, *s, color, scale);
}

SDLPrinter::SDLPrinter()
{
  isDirty = false;
  halted = false;
  haltShown = false;
  saveToastFrames = 0;
  saveToastPages = 0;
  saveToastMsg[0] = 0;
  ypos = 0;
  contentRows = 0;
  allocRows = 0;
  scrollY = 0;
  follow = true;

  _bitmap = NULL;
  _viewPixels = (uint32_t *)malloc((size_t)WIDTH * HEIGHT * sizeof(uint32_t));

  window = NULL;
  renderer = NULL;
  texture = NULL;

  printerMutex = SDL_CreateMutex();
}

SDLPrinter::~SDLPrinter()
{
  if (_bitmap) free(_bitmap);
  if (_viewPixels) free(_viewPixels);
  if (texture) SDL_DestroyTexture(texture);
  SDL_DestroyMutex(printerMutex);
}

// Grow the roll so it can hold at least `need` rows. On out-of-memory we keep
// the existing buffer; drawing then clamps to allocRows rather than crashing.
void SDLPrinter::ensureRows(uint32_t need)
{
  if (need > CAP_ROWS)     // never grow past the hard memory cap
    need = CAP_ROWS;
  if (need <= allocRows)
    return;

  uint32_t newRows = allocRows ? allocRows : HEIGHT;
  while (newRows < need)
    newRows += GROW_ROWS;
  if (newRows > CAP_ROWS)
    newRows = CAP_ROWS;

  uint8_t *nb = (uint8_t *)realloc(_bitmap, (size_t)WIDTH * newRows);
  if (!nb)
    return;

  memset(nb + (size_t)WIDTH * allocRows, 0, (size_t)WIDTH * (newRows - allocRows));
  _bitmap = nb;
  allocRows = newRows;
}

void SDLPrinter::update()
{
  if (!isDirty)
    return;

  // If we can't get the lock, something else is mid-draw; try again next frame.
  if (SDL_TryLockMutex(printerMutex))
    return;

  isDirty = false; // set early in case there's a race

  if (!window) {
    window = SDL_CreateWindow(WINDOWNAME,
                              SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              WIDTH, HEIGHT, SDL_WINDOW_SHOWN);
    renderer = SDL_CreateRenderer(window, -1, 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT);
  }

  // Keep the newest output in view unless the user has scrolled back.
  uint32_t maxScroll = (contentRows > HEIGHT) ? (contentRows - HEIGHT) : 0;
  if (follow)              scrollY = maxScroll;
  else if (scrollY > maxScroll) scrollY = maxScroll;

  // Build the viewport: black where a dot was printed, white paper elsewhere
  // (including any blank roll below the last line printed).
  for (uint32_t y = 0; y < HEIGHT; y++) {
    uint32_t src = scrollY + y;
    uint32_t *dst = &_viewPixels[(size_t)y * WIDTH];
    if (src < contentRows && _bitmap) {
      const uint8_t *b = &_bitmap[(size_t)src * WIDTH];
      for (uint32_t x = 0; x < WIDTH; x++)
        dst[x] = b[x] ? 0xFF000000u : 0xFFFFFFFFu;
    } else {
      for (uint32_t x = 0; x < WIDTH; x++)
        dst[x] = 0xFFFFFFFFu;
    }
  }

  // Scrollbar + page indicator, so a multi-page roll is obviously navigable and
  // page 1 doesn't just look "cleared" when the view follows to the newest page.
  uint32_t totalPages = (contentRows + HEIGHT - 1) / HEIGHT;
  if (totalPages < 1) totalPages = 1;
  if (contentRows > HEIGHT) {
    const int bxL = WIDTH - 11, bxR = WIDTH - 3;
    for (int y = 0; y < HEIGHT; y++)
      for (int x = bxL; x < bxR; x++)
        _viewPixels[(size_t)y * WIDTH + x] = 0xFFD7DBE1u; // track
    int thumbH = (int)((uint64_t)HEIGHT * HEIGHT / contentRows);
    if (thumbH < 28) thumbH = 28;
    int thumbY = (int)((uint64_t)scrollY * (HEIGHT - thumbH) / (contentRows - HEIGHT));
    for (int y = thumbY; y < thumbY + thumbH && y < HEIGHT; y++)
      for (int x = bxL; x < bxR; x++)
        _viewPixels[(size_t)y * WIDTH + x] = 0xFF8A909Cu; // thumb
  }
  {
    uint32_t curPage = (scrollY + HEIGHT / 2) / HEIGHT + 1;
    if (curPage > totalPages) curPage = totalPages;
    char lbl[40];
    snprintf(lbl, sizeof(lbl), "PAGE %u/%u", (unsigned)curPage, (unsigned)totalPages);
    int lw = (int)strlen(lbl) * 8 + 10;
    for (int y = 4; y < 20; y++)
      for (int x = 4; x < 4 + lw && x < WIDTH; x++)
        _viewPixels[(size_t)y * WIDTH + x] = 0xFF20252Fu; // label background
    stampText(_viewPixels, 9, 6, lbl, 0xFFEDEFF2u, 1);
  }

  // Roll is full: draw the "save & clear" prompt over the printout. The VM is
  // frozen by the main loop while halted; S saves, C clears/resumes (both work
  // regardless of window focus while halted).
  if (halted) {
    if (!haltShown) { raiseWindow(); haltShown = true; }
    const int bw = 720, bh = 152, bx = (WIDTH - bw) / 2, by = (HEIGHT - bh) / 2;
    const uint32_t fill = 0xFF181C24u, edge = 0xFFE0A020u, white = 0xFFFFFFFFu, amber = 0xFFE8B04Au;
    for (int y = by - 3; y < by + bh + 3; y++)
      for (int x = bx - 3; x < bx + bw + 3; x++) {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) continue;
        bool border = (y < by || y >= by + bh || x < bx || x >= bx + bw);
        _viewPixels[(size_t)y * WIDTH + x] = border ? edge : fill;
      }
    char t[48];
    snprintf(t, sizeof(t), "PRINTER FULL  %d PAGES", MAX_PAGES);
    stampText(_viewPixels, bx + 28, by + 22,  t, white, 2);
    stampText(_viewPixels, bx + 28, by + 56,  "VM PAUSED", amber, 2);
    stampText(_viewPixels, bx + 28, by + 92,  "S   SAVE PAGES AS PNG", white, 2);
    stampText(_viewPixels, bx + 28, by + 120, "C   CLEAR AND RESUME", white, 2);
  }

  // Brief "saved" confirmation over the (now cleared) roll. Kept alive by forcing
  // isDirty so update() keeps running until the frame counter expires.
  if (saveToastFrames > 0) {
    const int bw = 560, bh = 104, bx = (WIDTH - bw) / 2, by = (HEIGHT - bh) / 2;
    const uint32_t fill = 0xFF15221Au, edge = 0xFF37B26Fu, white = 0xFFFFFFFFu, green = 0xFF6FD89Bu;
    for (int y = by - 3; y < by + bh + 3; y++)
      for (int x = bx - 3; x < bx + bw + 3; x++) {
        if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) continue;
        bool border = (y < by || y >= by + bh || x < bx || x >= bx + bw);
        _viewPixels[(size_t)y * WIDTH + x] = border ? edge : fill;
      }
    char hd[48];
    snprintf(hd, sizeof(hd), "SAVED %u PAGE%s",
             (unsigned)saveToastPages, saveToastPages == 1 ? "" : "S");
    stampText(_viewPixels, bx + 26, by + 26, hd, green, 2);
    stampText(_viewPixels, bx + 26, by + 66, saveToastMsg, white, 1);
    saveToastFrames--;
    isDirty = true;   // keep redrawing until the confirmation expires
  }

  SDL_UpdateTexture(texture, NULL, _viewPixels, WIDTH * (int)sizeof(uint32_t));

  SDL_UnlockMutex(printerMutex);

  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, NULL, NULL);
  SDL_RenderPresent(renderer);
}

void SDLPrinter::addLine(uint8_t *rowOfBits)
{
  SDL_LockMutex(printerMutex);
  ensureRows(ypos + 9);

  for (int yoff = 0; yoff < 9; yoff++) {
    uint32_t row = ypos + yoff;
    if (row >= allocRows)
      break; // out of memory: stop rather than overrun
    // 960 pixels == 120 bytes -- FIXME
    for (int i = 0; i < (NATIVEWIDTH / 8); i++) {
      uint8_t bv = rowOfBits[yoff * 120 + i];
      for (int xoff = 0; xoff < 8; xoff++) {
        // scale X from "actual FX80" coordinates to "real printer" coordinates
        uint16_t actualX = (uint16_t)(((float)(i * 8 + xoff) * (float)WIDTH) / (float)NATIVEWIDTH);
        uint8_t pixelColor = (bv & (1 << (7 - xoff))) ? 0xFF : 0x00;
        // OR so we preserve pixels already drawn (scaling & overstrike).
        _bitmap[actualX + (size_t)row * WIDTH] |= pixelColor;
      }
    }
  }

  if (ypos + 9 > contentRows)
    contentRows = ypos + 9;
  if (!halted && contentRows >= LIMIT_ROWS)
    halted = true;

  isDirty = true;
  SDL_UnlockMutex(printerMutex);
}

void SDLPrinter::moveDownPixels(uint8_t p)
{
  SDL_LockMutex(printerMutex);
  // Advance down the roll. Form feeds simply move the head further, leaving the
  // blank gap between pages -- the roll grows instead of being cleared.
  ypos += p;
  ensureRows(ypos + 9);
  if (ypos > contentRows)
    contentRows = ypos;
  if (!halted && contentRows >= LIMIT_ROWS)
    halted = true;
  isDirty = true;
  SDL_UnlockMutex(printerMutex);
}

void SDLPrinter::clear()
{
  SDL_LockMutex(printerMutex);
  if (_bitmap && allocRows)
    memset(_bitmap, 0, (size_t)WIDTH * allocRows);
  ypos = 0;
  contentRows = 0;
  scrollY = 0;
  follow = true;
  halted = false;   // roll emptied: the VM can run again
  haltShown = false;
  isDirty = true;
  SDL_UnlockMutex(printerMutex);
}

void SDLPrinter::scrollByRows(int deltaRows)
{
  SDL_LockMutex(printerMutex);
  uint32_t maxScroll = (contentRows > HEIGHT) ? (contentRows - HEIGHT) : 0;
  long ns = (long)scrollY + deltaRows;
  if (ns < 0) ns = 0;
  if ((uint32_t)ns > maxScroll) ns = maxScroll;
  scrollY = (uint32_t)ns;
  // Resume auto-follow once the user scrolls back to the bottom.
  follow = (scrollY >= maxScroll);
  isDirty = true;
  SDL_UnlockMutex(printerMutex);
}

uint32_t SDLPrinter::windowID()
{
  return window ? SDL_GetWindowID(window) : 0;
}

bool SDLPrinter::isFocused()
{
  return window && SDL_GetKeyboardFocus() == window;
}

void SDLPrinter::raiseWindow()
{
  if (window)
    SDL_RaiseWindow(window);
}

// Take keyboard focus explicitly. macOS doesn't reliably make a secondary window
// of an already-active app the key window on a click, so SDL_GetKeyboardFocus()
// keeps pointing at the main window; SDL_SetWindowInputFocus forces it over.
void SDLPrinter::focusWindow()
{
  if (window) {
    SDL_RaiseWindow(window);
    SDL_SetWindowInputFocus(window);
  }
}

// --- minimal PNG writer (8-bit grayscale, zlib-deflated) --------------------
static void pngPut32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
  p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static void pngChunk(FILE *f, const char *type, const uint8_t *data, uint32_t len)
{
  uint8_t h[8];
  pngPut32(h, len); memcpy(h + 4, type, 4);
  fwrite(h, 1, 8, f);
  if (len) fwrite(data, 1, len, f);
  uLong crc = crc32(crc32(0, Z_NULL, 0), (const Bytef *)type, 4);
  if (len) crc = crc32(crc, (const Bytef *)data, len);
  uint8_t c[4]; pngPut32(c, (uint32_t)crc);
  fwrite(c, 1, 4, f);
}

// Write one page image: `nrows` rows of the roll starting at `startRow`, as an
// 8-bit grayscale PNG. Rows past the printed content (the tail of the last page)
// are white paper, so every page comes out a full, uniform sheet.
void SDLPrinter::writePngPage(const char *path, uint32_t startRow, uint32_t nrows)
{
  const size_t stride = (size_t)WIDTH + 1;   // filter byte + row
  const size_t rawLen = stride * nrows;
  uint8_t *raw = (uint8_t *)malloc(rawLen);
  if (!raw) { printf("PNG: out of memory\n"); return; }

  SDL_LockMutex(printerMutex);
  for (uint32_t y = 0; y < nrows; y++) {
    uint8_t *r = raw + (size_t)y * stride;
    r[0] = 0; // filter: none
    uint32_t src = startRow + y;
    const uint8_t *b = (src < contentRows && _bitmap) ? &_bitmap[(size_t)src * WIDTH] : NULL;
    for (uint32_t x = 0; x < WIDTH; x++)
      r[1 + x] = (b && b[x]) ? 0x00 : 0xFF;   // black dot vs white paper
  }
  SDL_UnlockMutex(printerMutex);   // the rest touches only our private copy

  uLongf compLen = compressBound(rawLen);
  uint8_t *comp = (uint8_t *)malloc(compLen);
  if (!comp) { free(raw); printf("PNG: out of memory\n"); return; }
  if (compress(comp, &compLen, raw, rawLen) != Z_OK) {
    free(raw); free(comp); printf("PNG: compression failed\n"); return;
  }
  free(raw);

  FILE *f = fopen(path, "wb");
  if (!f) { free(comp); perror(path); return; }
  static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
  fwrite(sig, 1, 8, f);
  uint8_t ihdr[13];
  pngPut32(ihdr + 0, WIDTH);
  pngPut32(ihdr + 4, nrows);
  ihdr[8] = 8;  // bit depth
  ihdr[9] = 0;  // color type: grayscale
  ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0; // deflate / filter / no interlace
  pngChunk(f, "IHDR", ihdr, 13);
  pngChunk(f, "IDAT", comp, (uint32_t)compLen);
  pngChunk(f, "IEND", NULL, 0);
  fclose(f);
  free(comp);
}

static bool pngExists(uint32_t n)
{
  char path[255];
  snprintf(path, sizeof(path), "page-%u.png", n);
  FILE *f = fopen(path, "rb");
  if (f) { fclose(f); return true; }
  return false;
}

// Lowest base such that page-{base}.png .. page-{base+count-1}.png are all free,
// so a save never clobbers earlier output and the numbering keeps growing on disk.
static uint32_t nextFreeBase(uint32_t count)
{
  for (uint32_t base = 0; ; base++) {
    uint32_t i = 0;
    while (i < count && !pngExists(base + i)) i++;
    if (i == count) return base;   // whole block is free
    base += i;                     // jump past the clash (loop's base++ steps once more)
  }
}

// Save the roll as discrete, page-sized PNGs -- one file per HEIGHT-tall page --
// into the next free page-N.png slots, so re-saving never overwrites earlier
// output. A printout is mostly white paper, so each deflated grayscale page is tiny.
void SDLPrinter::savePng()
{
  uint32_t pages = (contentRows + HEIGHT - 1) / HEIGHT;
  if (pages < 1) pages = 1;   // always emit at least one (possibly blank) page

  uint32_t base = nextFreeBase(pages);
  char path[255];
  for (uint32_t p = 0; p < pages; p++) {
    snprintf(path, sizeof(path), "page-%u.png", base + p);
    writePngPage(path, p * HEIGHT, HEIGHT);
  }
  printf("Saved printer roll: %u page%s (page-%u.png .. page-%u.png)\n",
         pages, pages == 1 ? "" : "s", base, base + pages - 1);

  // Show an on-screen "saved" confirmation, then clear the roll so the next job
  // starts on fresh paper. clear() leaves these toast fields alone so the
  // confirmation survives into the now-blank display.
  saveToastPages = pages;
  if (pages == 1)
    snprintf(saveToastMsg, sizeof(saveToastMsg), "page-%u.png", base);
  else
    snprintf(saveToastMsg, sizeof(saveToastMsg), "page-%u.png .. page-%u.png",
             base, base + pages - 1);
  saveToastFrames = 90;   // about 3 seconds at the display refresh
  clear();
}
