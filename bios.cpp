#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "globals.h"
#include "bios.h"
#include "usernet.h"   // unParseSubnet (validate the NAT subnet field)

#include "applevm.h"
#include "physicalkeyboard.h"
#include "physicaldisplay.h"
#include "cpu.h"
#include "appledisplay.h"
#include "font.h"

#ifdef TEENSYDUINO
#include <Bounce2.h>
#include "teensy-paddles.h"
#include "teensy-selfupdate.h"
#include "teensy-fwversion.h"
#include "protocol.h"   // AIIE_ESP_PROTO_VERSION / AIIE_ESP_FW_* for the ESP fw check
extern Bounce resetButtonDebouncer;
extern void runDebouncer();
#endif

// using EXTMEM to cache all the filenames in a directory
#ifndef TEENSYDUINO
#define EXTMEM
#endif

#define COLS 40
#define ROWS 24
#define CELLW 8
#define CELLH 10

enum {
  ST_NORMAL = 0,
  ST_INV,
  ST_DIM,
  ST_DIMINV,
  ST_PLAIN,
};

#define C_WHITE 0xFFFF
#define C_BLUE  0x0030
#define C_GRAY  0x7BEF
#define C_BLACK 0x0000

#define A_UP "\x8B"
#define A_DN "\x8A"
#define A_LT "\x88"
#define A_RT "\x95"

static uint8_t g_scale = 1;
static uint16_t g_ox = 0, g_oy = 0;

static void layout()
{
  uint16_t w = g_display->width(), h = g_display->height();
  uint8_t sx = (uint8_t)(w / (COLS * CELLW)), sy = (uint8_t)(h / (ROWS * CELLH));
  g_scale = sx < sy ? sx : sy;
  if (g_scale < 1) g_scale = 1;
  g_ox = (uint16_t)((w - COLS * CELLW * g_scale) / 2);
  g_oy = (uint16_t)((h - ROWS * CELLH * g_scale) / 2);
}

static void rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  uint16_t x0 = (uint16_t)(g_ox + x * g_scale), y0 = (uint16_t)(g_oy + y * g_scale);
  for (uint16_t py = 0; py < h * g_scale; py++)
    for (uint16_t px = 0; px < w * g_scale; px++)
      g_display->drawPixel((uint16_t)(x0 + px), (uint16_t)(y0 + py), color);
}

static void frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
  rect(x, y, w, 1, color);
  rect(x, (uint16_t)(y + h - 1), w, 1, color);
  rect(x, y, 1, h, color);
  rect((uint16_t)(x + w - 1), y, 1, h, color);
}

static void styleColors(uint8_t st, uint16_t *fg, uint16_t *bg)
{
  switch (st) {
  default:
  case ST_NORMAL: *fg = C_WHITE; *bg = C_BLUE;  break;
  case ST_INV:    *fg = C_BLUE;  *bg = C_WHITE; break;
  case ST_DIM:    *fg = C_GRAY;  *bg = C_BLUE;  break;
  case ST_DIMINV: *fg = C_GRAY;  *bg = C_WHITE; break;
  case ST_PLAIN:  *fg = C_WHITE; *bg = C_BLACK; break;
  }
}

static void cell(uint8_t row, uint8_t col, unsigned char c, uint8_t st)
{
  if (row >= ROWS || col >= COLS) return;
  uint16_t fg, bg;
  styleColors(st, &fg, &bg);
  const unsigned char *g = asciiToAppleGlyph(c);
  uint16_t x0 = (uint16_t)(g_ox + col * CELLW * g_scale);
  uint16_t y0 = (uint16_t)(g_oy + row * CELLH * g_scale);
  for (uint8_t gy = 0; gy < CELLH; gy++) {
    unsigned char bits = (gy >= 1 && gy <= 8) ? g[gy - 1] : 0;
    for (uint8_t gx = 0; gx < CELLW; gx++) {
      uint16_t color = (bits & (1 << gx)) ? fg : bg;
      for (uint8_t sy = 0; sy < g_scale; sy++)
        for (uint8_t sx = 0; sx < g_scale; sx++)
          g_display->drawPixel((uint16_t)(x0 + gx * g_scale + sx),
                               (uint16_t)(y0 + gy * g_scale + sy), color);
    }
  }
}

static void put(uint8_t row, uint8_t col, const char *s, uint8_t st)
{
  while (*s && col < COLS)
    cell(row, col++, (unsigned char)*s++, st);
}

static void putn(uint8_t row, uint8_t col, const char *s, uint8_t n, uint8_t st)
{
  for (uint8_t i = 0; i < n && col < COLS; i++, col++)
    cell(row, col, (unsigned char)(*s ? *s++ : ' '), st);
}

static void putName(uint8_t row, uint8_t col, const char *s, uint8_t n, uint8_t st)
{
  char buf[COLS + 1];
  size_t len = strlen(s);
  if (len <= n || n < 8) {
    putn(row, col, s, n, st);
    return;
  }
  uint8_t head = (uint8_t)((n - 1) / 2), tail = (uint8_t)(n - 1 - head);
  snprintf(buf, sizeof(buf), "%.*s~%s", head, s, s + len - tail);
  putn(row, col, buf, n, st);
}

static void putCentered(uint8_t row, const char *s, uint8_t st)
{
  size_t n = strlen(s);
  uint8_t col = (n >= COLS) ? 0 : (uint8_t)((COLS - n) / 2);
  put(row, col, s, st);
}

static void bar(uint8_t row, const char *s)
{
  for (uint8_t i = 0; i < COLS; i++)
    cell(row, i, ' ', ST_INV);
  putCentered(row, s, ST_INV);
}

static void clear(const char *title)
{
  g_display->clrScr(c_darkblue);
  rect(0, 0, COLS * CELLW, ROWS * CELLH, C_BLUE);
  bar(0, title);
  bar(ROWS - 1, " The //e is frozen ");
}

static void splash(const char *msg)
{
  clear(" AIIE ");
  putCentered(11, msg, ST_NORMAL);
  g_display->flush();
}

static const char *buildStamp()
{
#ifdef TEENSYDUINO
  return g_fwVersion;
#else
  return __DATE__;
#endif
}

static const char *baseName(const char *path)
{
  const char *p = strrchr(path, '/');
  return p ? p + 1 : path;
}

enum {
  SCR_MAIN = 0,
  SCR_CARDS,
  SCR_DISPLAY,
  SCR_PADDLES,
  SCR_NETWORK,
  SCR_ADVANCED,
  SCR_ABOUT,
  SCR_BROWSE,
  SCR_NOTICE,
  SCR_DONE,
};

#define NOKEY (-1)

enum {
  IT_RESUME = 0,
  IT_RESET,
  IT_REBOOT,
  IT_EJECTBOOT,
  IT_FD0,
  IT_FD1,
  IT_HD0,
  IT_HD1,
  IT_CARDS,
  IT_DISPLAY,
  IT_PADDLES,
  IT_NETWORK,
  IT_VOLUME,
  IT_SPEED,
  IT_ADVANCED,
  IT_ABOUT,
  NITEMS
};

static const char * const items[NITEMS] = {
  "Resume",
  "Reset the //e (warm)",
  "Reboot the //e (cold)",
  "Reboot and eject disks",
  "Disk drive 1...",
  "Disk drive 2...",
  "Hard drive 1...",
  "Hard drive 2...",
  "Cards...",
  "Display...",
  "Paddles...",
  "Network...",
  "Volume",
  "CPU speed",
  "Advanced...",
  "About Aiie",
};

#define NGROUPGAPS 3
#define ITEMGAPS(i) ((uint8_t)(((i) > IT_EJECTBOOT) + ((i) > IT_HD1) + ((i) > IT_SPEED)))
#define ITEMTOP ((uint8_t)((ROWS - (NITEMS + NGROUPGAPS)) / 2))
#define ITEMROW(i) ((uint8_t)(ITEMTOP + (i) + ITEMGAPS(i)))
#define HELPROW ((uint8_t)(ITEMTOP + NITEMS + NGROUPGAPS + 1))

#define LCOL 3
#define VCOL 21
#define VWIDTH (COLS - VCOL)

static const struct {
  uint32_t hz;
  const char *name;
} speeds[] = {
  { 1023000 / 2,   "1/2x (511.5 kHz)" },
  { 1023000,       "1x (1.023 MHz)" },
  { 1023000 * 2,   "2x (2.046 MHz)" },
  { 1023000 * 4,   "4x (4.092 MHz)" },
  // The Teensy can't sustain more than 4x
#ifndef TEENSYDUINO
  { 1023000 * 8,   "8x (8.184 MHz)" },
  { 1023000 * 16,  "16x (16.37 MHz)" },
  // Audio is muted at 128x and beyond; the speaker would have to
  // time-compress 128 seconds of toggles into 1, which is just noise.
  { 1023000 * 128, "128x (no audio)" },
  { 1023000 * 256, "256x (no audio)" },
#endif
};
#define NSPEEDS ((uint8_t)(sizeof(speeds) / sizeof(speeds[0])))

static uint8_t speedIndexFor(uint32_t hz)
{
  for (uint8_t i = 0; i < NSPEEDS; i++)
    if (speeds[i].hz == hz) return i;
  // Dunno what happened, but we'll default back to full (normal) speed
  return 1;
}

static const char *debugModeName(uint8_t m)
{
  switch (m) {
  case D_SHOWFPS:     return "FPS";
  case D_SHOWMEMFREE: return "memory free";
  case D_SHOWPADDLES: return "paddles";
  case D_SHOWPC:      return "PC";
  case D_SHOWCYCLES:  return "cycles";
  case D_SHOWBATTERY: return "battery";
  case D_SHOWTIME:    return "time";
  case D_SHOWDSK:     return "disk";
  case D_SHOWNET:     return "network";
  default:            return "off";
  }
}
#define NDEBUGMODES 10

static const char *displayTypeName(uint8_t t)
{
  switch (t) {
  case m_blackAndWhite: return "B&W";
  case m_monochrome:    return "Mono";
  case m_ntsclike:      return "NTSC-like";
  case m_perfectcolor:  return "RGB";
  }
  return "?";
}
#define NDISPLAYTYPES 4

static void displayChanged()
{
  if (g_vm && g_vm->vmdisplay)
    ((AppleDisplay *)g_vm->vmdisplay)->displayTypeChanged();
}

static uint8_t savedSlotDiskII;
static uint8_t savedSlotParallel;
static uint8_t savedSlotHD32;
static uint8_t savedSlotMouse;
static uint8_t savedSlotMockingboard;
static uint8_t savedSlotUthernet;
static uint8_t savedRamworksSize;
static bool cardsConfigChanged = false;

static bool hdChanged = false;

static void saveCardConfig()
{
  savedSlotDiskII = g_slotDiskII;
  savedSlotParallel = g_slotParallel;
  savedSlotHD32 = g_slotHD32;
  savedSlotMouse = g_slotMouse;
  savedSlotMockingboard = g_slotMockingboard;
  savedSlotUthernet = g_slotUthernet;
  savedRamworksSize = g_ramworksSize;
  cardsConfigChanged = false;
}

static bool slotsMatchSaved()
{
  return (g_slotDiskII == savedSlotDiskII &&
          g_slotParallel == savedSlotParallel &&
          g_slotHD32 == savedSlotHD32 &&
          g_slotMouse == savedSlotMouse &&
          g_slotMockingboard == savedSlotMockingboard &&
          g_slotUthernet == savedSlotUthernet &&
          g_ramworksSize == savedRamworksSize);
}

#define NCARDS 6
static const struct {
  const char *name;
  uint8_t *slot;
} cards[NCARDS] = {
  { "Disk II",          &g_slotDiskII },
  { "Parallel printer", &g_slotParallel },
  { "Hard disk",        &g_slotHD32 },
  { "Mouse",            &g_slotMouse },
  { "Mockingboard",     &g_slotMockingboard },
  { "Uthernet II",      &g_slotUthernet },
};
#define CARD_MOUSE 3
#define CARD_RW NCARDS
#define CARD_DEFAULTS (NCARDS + 1)
#define NCARDROWS (NCARDS + 2)

// Slots a card can be assigned to. 0 means "disabled". Slot 3 is allowed: an
// I/O-only card (e.g. the Uthernet) coexists with the internal 80-column
// firmware, which the MMU now keeps in a separate bank from a slot-3 card's ROM
// (see _slotRomPageForSlot in applemmu.cpp). A card WITH a boot ROM in slot 3 is
// reachable only via SETC3ROM, so it is not seen by the boot scan.
static const uint8_t kSelectableSlots[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
#define NSLOTCHOICES ((int)sizeof(kSelectableSlots))

static bool isSelectableSlot(uint8_t n)
{
  for (int i = 0; i < NSLOTCHOICES; i++)
    if (kSelectableSlots[i] == n) return true;
  return false;
}

// Cycle through the available RamWorks sizes. Embedded builds
// are capped at 1MB; the desktop offers the full range.
#ifdef TEENSYDUINO
static const uint8_t rwSizes[] = { 0, 1 };
#else
static const uint8_t rwSizes[] = { 0, 1, 3, 16 };
#endif
#define NRWSIZES ((int)sizeof(rwSizes))

static void resolveSlotConflict(uint8_t *changedVar)
{
  uint8_t newSlot = *changedVar;
  if (newSlot == 0) return;

  for (int i = 0; i < NCARDS; i++) {
    uint8_t *other = cards[i].slot;
    if (other == changedVar || *other != newSlot) continue;
    // Find an available slot for the displaced card
    static const uint8_t validSlots[] = { 1, 2, 4, 5, 6, 7 };
    bool found = false;
    for (size_t s = 0; s < sizeof(validSlots) && !found; s++) {
      uint8_t candidate = validSlots[s];
      bool taken = false;
      for (int j = 0; j < NCARDS; j++) {
        if (cards[j].slot != other && *cards[j].slot == candidate) {
          taken = true;
          break;
        }
      }
      if (!taken) {
        *other = candidate;
        found = true;
      }
    }
    if (!found)
      *other = 0;
  }
}

static bool placeCard(int card, uint8_t slot)
{
  uint8_t *var = cards[card].slot;
  if (!isSelectableSlot(slot)) return false;
  // The mouse has a boot ROM, so in slot 3 it would sit behind the internal
  // 80-column firmware and never be found. Any other slot works.
  if (card == CARD_MOUSE && slot == 3) return false;
  *var = slot;
  if (slot != 0) resolveSlotConflict(var); // move any card already there
  cardsConfigChanged = !slotsMatchSaved();
  return true;
}

static void stepCard(int card, int dir)
{
  if (card == CARD_RW) {
    int idx = 0;
    for (int i = 0; i < NRWSIZES; i++)
      if (rwSizes[i] == g_ramworksSize) { idx = i; break; }
    g_ramworksSize = rwSizes[(idx + dir + NRWSIZES) % NRWSIZES];
    cardsConfigChanged = !slotsMatchSaved();
    return;
  }
  uint8_t cur = *cards[card].slot;
  int idx = 0;
  for (int i = 0; i < NSLOTCHOICES; i++)
    if (kSelectableSlots[i] == cur) { idx = i; break; }
  idx = (idx + dir + NSLOTCHOICES) % NSLOTCHOICES;
  if (card == CARD_MOUSE && kSelectableSlots[idx] == 3)   // step past slot 3
    idx = (idx + dir + NSLOTCHOICES) % NSLOTCHOICES;
  placeCard(card, kSelectableSlots[idx]);
}

static void typeCardDigit(int card, uint8_t d)
{
  if (card == CARD_RW) {
    for (int i = 0; i < NRWSIZES; i++)
      if (rwSizes[i] == d) {
        g_ramworksSize = d;
        cardsConfigChanged = !slotsMatchSaved();
      }
    return;
  }
  placeCard(card, d);
}

static void defaultCards()
{
  g_slotDiskII = 6;
  g_slotParallel = 1;
  g_slotHD32 = 7;
  g_slotMouse = 0;   // off by default
  g_slotMockingboard = 4;
  g_slotUthernet = 0;
  g_ramworksSize = 0;
  cardsConfigChanged = !slotsMatchSaved();
}

struct _cacheEntry {
  char fn[BIOS_MAXPATH];
};
#define BIOSCACHESIZE 2048
EXTMEM static char cachedPath[255 - BIOS_MAXPATH] = {0};
EXTMEM static char cachedFilter[16] = {0};
EXTMEM static struct _cacheEntry biosCache[BIOSCACHESIZE];
static uint16_t numCacheEntries = 0;

EXTMEM static uint16_t visible[BIOSCACHESIZE];
static uint16_t numVisible = 0;

#define KIND_FD 0
#define KIND_HD 1
#define PAGESZ 16
#define LISTTOP 2
#define NAMEWIDTH 35

static const char *browseFilter(uint8_t kind)
{
  return (kind == KIND_FD) ? "dsk,.po,nib,woz" : "img,hdv,2mg";
}

static bool isDirEntry(const char *fn)
{
  size_t n = strlen(fn);
  return n && fn[n - 1] == '/';
}

static int compareEntries(const void *a, const void *b)
{
  const char *fa = ((const struct _cacheEntry *)a)->fn;
  const char *fb = ((const struct _cacheEntry *)b)->fn;
  bool da = isDirEntry(fa), db = isDirEntry(fb);
  if (da != db) return da ? -1 : 1;
  if (da && !strcmp(fa, "../")) return -1;
  if (db && !strcmp(fb, "../")) return 1;
  return strcasecmp(fa, fb);
}

static bool nameMatches(const char *name, const char *find)
{
  if (!*find) return true;
  size_t n = strlen(find);
  for (const char *p = name; *p; p++) {
    size_t j;
    for (j = 0; j < n; j++) {
      char a = p[j], b = find[j];
      if (a >= 'a' && a <= 'z') a = (char)(a - 32);
      if (b >= 'a' && b <= 'z') b = (char)(b - 32);
      if (a != b) break;
    }
    if (j == n) return true;
  }
  return false;
}

const char *staticPathConcat(const char *rootPath, const char *filePath)
{
  static char buf[MAXPATH];
  strncpy(buf, rootPath, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  strncat(buf, filePath, sizeof(buf) - strlen(buf) - 1);
  return buf;
}

BIOS::BIOS()
{
  screen = SCR_DONE;
  mainSel = IT_RESUME;
  sel = 0;
  speedIndex = 1;
  browseKind = KIND_FD;
  browseDrive = 0;
  browseTop = browseSel = 0;
  findText[0] = 0;
  rootPath[0] = 0;
  noticeA = noticeB = "";
  noticeBack = SCR_MAIN;
  saveCardConfig();
}

BIOS::~BIOS()
{
}

static void setSpeed(uint8_t idx)
{
  if (idx >= NSPEEDS) idx = 1;
  g_speed = speeds[idx].hz;
}

bool BIOS::loop()
{
  static bool needsInit = true;
  if (needsInit) {
    g_filemanager->getRootPath(rootPath, sizeof(rootPath));
    needsInit = false;
  }

  static bool redraw = true;

  if (screen == SCR_DONE) {
    layout();
    screen = SCR_MAIN;
    mainSel = IT_RESUME;
    sel = 0;
    redraw = true;
    saveCardConfig();
    hdChanged = false;
    // Need to initialize the CPU speed from g_speed
    speedIndex = speedIndexFor(g_speed);
    setSpeed(speedIndex);
  }

  int key = NOKEY;

#ifdef TEENSYDUINO
  if (resetButtonDebouncer.read() == LOW) {
    // wait until it's no longer pressed
    while (resetButtonDebouncer.read() == LOW)
      runDebouncer();
    delay(100); // wait long enough for it to debounce
    key = PK_ESC;
  }
#endif

  if (key == NOKEY && g_keyboard->kbhit()) {
    uint8_t k = (uint8_t)g_keyboard->read();
    if (k != (uint8_t)PK_NONE && k != 0)
      key = k;
  }

  for (int pass = 0; pass < 3; pass++) {
    uint8_t was = screen;
    uint8_t next;
    switch (screen) {
    case SCR_MAIN:     next = mainScreen(redraw, key);     break;
    case SCR_CARDS:    next = cardsScreen(redraw, key);    break;
    case SCR_DISPLAY:  next = displayScreen(redraw, key);  break;
    case SCR_PADDLES:  next = paddlesScreen(redraw, key);  break;
    case SCR_NETWORK:  next = networkScreen(redraw, key);  break;
    case SCR_ADVANCED: next = advancedScreen(redraw, key); break;
    case SCR_ABOUT:    next = aboutScreen(redraw, key);    break;
    case SCR_BROWSE:   next = browseScreen(redraw, key);   break;
    case SCR_NOTICE:   next = messageScreen(redraw, key);  break;
    default:           next = SCR_DONE;                    break;
    }

    if (was == SCR_NETWORK && next != SCR_NETWORK) {
      // Left the Net tab (via ESC, a tab switch, or resume): push any edited
      // inbound-forward list / port offset to the live NAT so it takes effect
      // this session without a restart.
      if (g_uthernet) g_uthernet->applyForwardConfig();
    }

    if (next == screen) {
      redraw = false;
      break;
    }
    screen = next;
    redraw = true;
    key = NOKEY;
    if (screen != SCR_MAIN) sel = 0;
    if (screen == SCR_DONE) break;
  }

  return screen != SCR_DONE;
}

uint8_t BIOS::leaveBIOS()
{
  if (cardsConfigChanged) {
    splash("Reconfiguring slots...");
    ((AppleVM *)g_vm)->reassignSlots();
    saveCardConfig();
  }
  return SCR_DONE;
}

static void drawDriveRow(uint8_t item, bool selected)
{
  bool hd = (item == IT_HD0 || item == IT_HD1);
  uint8_t drive = (item == IT_FD1 || item == IT_HD1) ? 1 : 0;
  bool haveCard = hd ? (g_slotHD32 != 0) : (g_slotDiskII != 0);
  uint8_t row = ITEMROW(item);

  if (!haveCard) {
    char buf[24];
    snprintf(buf, sizeof(buf), "(%s)", items[item]);
    char *dots = strstr(buf, "...");
    if (dots) { dots[0] = ')'; dots[1] = 0; }
    put(row, LCOL, buf, selected ? ST_DIMINV : ST_DIM);
    put(row, VCOL, "no card", ST_DIM);
    return;
  }
  put(row, LCOL, items[item], selected ? ST_INV : ST_NORMAL);
  const char *name = hd ? ((AppleVM *)g_vm)->HDName(drive)
    : ((AppleVM *)g_vm)->DiskName(drive);
  if (name && name[0])
    // Get the name of the file; strip off the directory
    putName(row, VCOL, baseName(name), VWIDTH, ST_NORMAL);
  else
    put(row, VCOL, "(empty)", ST_DIM);
}

void BIOS::drawMain()
{
  char buf[40];

  clear("");
  put(0, 1, " AIIE ", ST_INV);
  {
    const char *stamp = buildStamp();
    size_t n = strlen(stamp);
    if (n < COLS - 8) put(0, (uint8_t)(COLS - 1 - n), stamp, ST_INV);
  }

  for (uint8_t i = 0; i < NITEMS; i++) {
    bool selected = (i == mainSel);
    switch (i) {
    case IT_RESUME:
      // Resume continues the running VM. That is unsafe once a card slot (or the
      // RamWorks size) has changed under it, because the booted OS still sees the
      // old hardware; so disable Resume until the user resets or reboots.
      if (cardsConfigChanged)
        put(ITEMROW(i), LCOL, "(Resume)", selected ? ST_DIMINV : ST_DIM);
      else
        put(ITEMROW(i), LCOL, items[i], selected ? ST_INV : ST_NORMAL);
      break;
    case IT_FD0: case IT_FD1: case IT_HD0: case IT_HD1:
      drawDriveRow(i, selected);
      break;
    default:
      put(ITEMROW(i), LCOL, items[i], selected ? ST_INV : ST_NORMAL);
      break;
    }
  }

  snprintf(buf, sizeof(buf), "%d", g_volume);
  put(ITEMROW(IT_VOLUME), VCOL, buf, ST_NORMAL);
  {
    uint16_t x = (uint16_t)((VCOL + 3) * CELLW), y = (uint16_t)(ITEMROW(IT_VOLUME) * CELLH + 2);
    uint16_t w = 15 * CELLW;
    // draw the volume bar
    frame(x, y, (uint16_t)(w + 2), 6, C_GRAY);
    if (g_volume > 0)
      rect((uint16_t)(x + 1), (uint16_t)(y + 1), (uint16_t)(w * g_volume / 15), 4, C_WHITE);
  }
  put(ITEMROW(IT_SPEED), VCOL, speeds[speedIndex].name, ST_NORMAL);

  // Explain why Resume is greyed out after a card/RamWorks change.
  if (cardsConfigChanged)
    putCentered(HELPROW, "Cards changed: Reset or Reboot to apply", ST_NORMAL);
  else if (hdChanged)
    putCentered(HELPROW, "Hard drive changed: reboot to use it", ST_NORMAL);
  else
    putCentered(HELPROW, A_UP A_DN " move   " A_LT A_RT " set   Esc resumes", ST_DIM);

  g_display->flush();
}

uint8_t BIOS::mainAction(int row)
{
  switch (row) {
  case IT_RESUME:
    if (cardsConfigChanged) return SCR_MAIN;
    return leaveBIOS();
  case IT_RESET:
    leaveBIOS();
    WarmReset();
    return SCR_DONE;
  case IT_REBOOT:
    // Reboot, but don't eject disks
    leaveBIOS();
    RebootAsIs();
    return SCR_DONE;
  case IT_EJECTBOOT:
    // Power off and on, ejecting disks
    leaveBIOS();
    ColdReboot();
    return SCR_DONE;
  case IT_FD0:
  case IT_FD1:
    if (!g_slotDiskII)
      return notice("No Disk II controller.", "Install one under Cards.", SCR_MAIN);
    openBrowser(KIND_FD, (uint8_t)(row - IT_FD0));
    return SCR_BROWSE;
  case IT_HD0:
  case IT_HD1:
    if (!g_slotHD32)
      return notice("No hard drive card.", "Install one under Cards.", SCR_MAIN);
    openBrowser(KIND_HD, (uint8_t)(row - IT_HD0));
    return SCR_BROWSE;
  case IT_CARDS:    return SCR_CARDS;
  case IT_DISPLAY:  return SCR_DISPLAY;
  case IT_PADDLES:  return SCR_PADDLES;
  case IT_NETWORK:  return SCR_NETWORK;
  case IT_ADVANCED: return SCR_ADVANCED;
  case IT_ABOUT:    return SCR_ABOUT;
  case IT_VOLUME:
    g_volume = (int8_t)((g_volume >= 15) ? 0 : g_volume + 1);
    return SCR_MAIN;
  case IT_SPEED:
    speedIndex = (uint8_t)((speedIndex + 1) % NSPEEDS);
    setSpeed(speedIndex);
    return SCR_MAIN;
  }
  return SCR_MAIN;
}

uint8_t BIOS::mainScreen(bool redraw, int key)
{
  bool paint = redraw;

  switch (key) {
  case NOKEY:
    break;
  case PK_UARR:
    mainSel = (uint8_t)(mainSel ? mainSel - 1 : NITEMS - 1);
    paint = true;
    break;
  case PK_DARR:
    mainSel = (uint8_t)((mainSel + 1) % NITEMS);
    paint = true;
    break;
  case PK_LARR:
  case PK_RARR:
    {
      int dir = (key == PK_RARR) ? 1 : -1;
      if (mainSel == IT_VOLUME) {
        int v = g_volume + dir;
        if (v < 0) v = 0;
        if (v > 15) v = 15;
        g_volume = (int8_t)v;
      } else if (mainSel == IT_SPEED) {
        int v = speedIndex + dir;
        if (v < 0) v = 0;
        if (v >= NSPEEDS) v = NSPEEDS - 1;
        speedIndex = (uint8_t)v;
        setSpeed(speedIndex);
      }
      paint = true;
    }
    break;
  case PK_ESC:
    if (cardsConfigChanged) {
      mainSel = IT_REBOOT;
      paint = true;
      break;
    }
    return leaveBIOS();
  case PK_RET:
    {
      uint8_t next = mainAction(mainSel);
      if (next != SCR_MAIN) return next;
      paint = true;
    }
    break;
  default:
    break;
  }

  if (paint) drawMain();
  return SCR_MAIN;
}

void BIOS::drawCards()
{
  char buf[24];

  clear(" CARDS ");
  for (int i = 0; i < NCARDROWS; i++) {
    uint8_t row = (uint8_t)(3 + i + (i == CARD_DEFAULTS ? 1 : 0));
    bool selected = (sel == i);
    if (i == CARD_DEFAULTS) {
      put(row, LCOL, "Reset to defaults", selected ? ST_INV : ST_NORMAL);
      continue;
    }
    if (i == CARD_RW) {
      put(row, LCOL, "RamWorks", selected ? ST_INV : ST_NORMAL);
      if (g_ramworksSize) {
        snprintf(buf, sizeof(buf), "%dMB aux", g_ramworksSize);
        put(row, VCOL, buf, ST_NORMAL);
      } else
        put(row, VCOL, "not installed", ST_DIM);
      continue;
    }
    put(row, LCOL, cards[i].name, selected ? ST_INV : ST_NORMAL);
    uint8_t s = *cards[i].slot;
    if (s) {
      snprintf(buf, sizeof(buf), "slot %d", s);
      put(row, VCOL, buf, ST_NORMAL);
    } else
      put(row, VCOL, "not installed", ST_DIM);
  }

  put(14, 2, "A digit is the slot; 0 removes it.", ST_DIM);
  put(15, 2, "On RamWorks it is the size in MB.", ST_DIM);
  put(16, 2, "Slot 3 suits only the Uthernet.", ST_DIM);
  put(17, 2, "The mouse takes any other slot.", ST_DIM);
  put(19, 2, "Up/Down move, L/R or Return set.", ST_DIM);
  put(20, 2, "Esc returns.", ST_DIM);

  if (cardsConfigChanged)
    bar(ROWS - 1, " Resume is off until a restart ");
  else
    bar(ROWS - 1, " A change restarts the //e ");
  g_display->flush();
}

uint8_t BIOS::cardsScreen(bool redraw, int key)
{
  bool paint = redraw;

  switch (key) {
  case NOKEY:
    break;
  case PK_ESC:
    return SCR_MAIN;
  case PK_UARR:
    sel = (uint8_t)(sel ? sel - 1 : NCARDROWS - 1);
    paint = true;
    break;
  case PK_DARR:
    sel = (uint8_t)((sel + 1) % NCARDROWS);
    paint = true;
    break;
  case PK_LARR:
    if (sel < CARD_DEFAULTS) stepCard(sel, -1);
    paint = true;
    break;
    // Return, '+' and '=' all advance the selected entry to its next value; '-'
    // steps backward. This gives the slot/size rows a two-way cycle from the
    // keyboard, on top of the direct digit entry above.
  case PK_RARR:
  case PK_RET:
  case '+': case '=':
    if (sel == CARD_DEFAULTS) {
      if (key == PK_RET) defaultCards();
    } else
      stepCard(sel, 1);
    paint = true;
    break;
  case '-':
    if (sel < CARD_DEFAULTS) stepCard(sel, -1);
    paint = true;
    break;
  default:
    // Press a digit to put the selected card directly in that slot (0 disables
    // it). Only acts on a card row, and only for a selectable slot; other digits
    // (8, 9) are ignored rather than cycling anything.
    if (key >= '0' && key <= '9' && sel < CARD_DEFAULTS) {
      typeCardDigit(sel, (uint8_t)(key - '0'));
      paint = true;
    }
    break;
  }

  if (paint) drawCards();
  return SCR_CARDS;
}

#define DS_TYPE 0
#define DS_VIDEO7 1
#define DS_LUMA 2
#define NDISPLAYROWS 3

void BIOS::drawDisplay()
{
  char buf[24];

  clear(" DISPLAY ");
  put(3, LCOL, "Display type", sel == DS_TYPE ? ST_INV : ST_NORMAL);
  put(3, VCOL, displayTypeName(g_displayType), ST_NORMAL);

  put(4, LCOL, "Video7 color text", sel == DS_VIDEO7 ? ST_INV : ST_NORMAL);
  // The renderer also requires a color display type, so say so
  // rather than leaving the user to wonder why "Yes" changed
  // nothing. See video7.md and redraw40ColumnText().
  if (g_video7 && g_displayType != m_ntsclike && g_displayType != m_perfectcolor)
    put(4, VCOL, "yes (needs color)", ST_NORMAL);
  else
    put(4, VCOL, g_video7 ? "yes" : "no", ST_NORMAL);

  put(5, LCOL, "Luminance cutoff", sel == DS_LUMA ? ST_INV : ST_NORMAL);
  snprintf(buf, sizeof(buf), "%d", g_luminanceCutoff);
  put(5, VCOL, buf, ST_NORMAL);
  put(8, 2, "B&W and Mono draw one color; NTSC-like", ST_DIM);
  put(9, 2, "draws NTSC artifacts, while RGB draws", ST_DIM);
  put(10, 2, "\"perfect\" pixel colors that skip", ST_DIM);
  put(11, 2, "columns (Apple ][ artifacts).", ST_DIM);
  put(13, 2, "Video7 adds color attributes for", ST_DIM);
  put(14, 2, "fg/bg text like a IIgs.", ST_DIM);
  put(16, 2, "The cutoff is the brightness at which", ST_DIM);
  put(17, 2, "a B&W pixel is lit; -/+ step it by 16.", ST_DIM);

  put(19, 2, "Up/Down move, L/R set. Esc returns.", ST_DIM);
  g_display->flush();
}

uint8_t BIOS::displayScreen(bool redraw, int key)
{
  bool paint = redraw;
  int dir = 0;

  switch (key) {
  case NOKEY:
    break;
  case PK_ESC:
    return SCR_MAIN;
  case PK_UARR:
    sel = (uint8_t)(sel ? sel - 1 : NDISPLAYROWS - 1);
    paint = true;
    break;
  case PK_DARR:
    sel = (uint8_t)((sel + 1) % NDISPLAYROWS);
    paint = true;
    break;
  case PK_LARR: dir = -1;  break;
  case PK_RARR: dir = 1;   break;
  case PK_RET:  dir = 1;   break;
  case '-':     dir = -16; break;
  case '+': case '=': dir = 16; break;
  default:
    break;
  }

  if (dir) {
    switch (sel) {
    case DS_TYPE:
      g_displayType = (uint8_t)((g_displayType + (dir > 0 ? 1 : NDISPLAYTYPES - 1)) % NDISPLAYTYPES);
      displayChanged();
      break;
    case DS_VIDEO7:
      // Whether the machine has an RGB card with the Video7 extensions,
      // which turn aux text page 1 into a color-attribute plane for
      // 40-column text. See video7.md. It takes effect on the next
      // frame; the //e does not need to be reset for it.
      g_video7 = !g_video7;
      break;
    case DS_LUMA:
      {
        int v = g_luminanceCutoff + dir;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        g_luminanceCutoff = (uint8_t)v;
        displayChanged();
      }
      break;
    }
    paint = true;
  }

  if (paint) drawDisplay();
  return SCR_DISPLAY;
}

#define PD_X 0
#define PD_Y 1
#define NPADDLEROWS 2

#define PADBOX_X 200
#define PADBOX_Y 60
#define PADBOX_W 101

void BIOS::drawPaddles()
{
  char buf[24];
  uint8_t px = g_paddles->paddle0(), py = g_paddles->paddle1();

  clear(" PADDLES ");
  put(3, LCOL, "Paddle X", sel == PD_X ? ST_INV : ST_NORMAL);
  put(3, 13, g_invertPaddleX ? "inverted" : "normal", ST_NORMAL);
  put(4, LCOL, "Paddle Y", sel == PD_Y ? ST_INV : ST_NORMAL);
  put(4, 13, g_invertPaddleY ? "inverted" : "normal", ST_NORMAL);

  snprintf(buf, sizeof(buf), "X: %3d", px);
  put(8, LCOL, buf, ST_NORMAL);
  snprintf(buf, sizeof(buf), "Y: %3d", py);
  put(9, LCOL, buf, ST_NORMAL);

  put(12, 2, "Move the paddles or", ST_DIM);
  put(13, 2, "the mouse to test.", ST_DIM);
  put(19, 2, "Up/Down move, L/R flip. Esc to return.", ST_DIM);

  // Draw the target for the paddle position
  rect(PADBOX_X, PADBOX_Y, PADBOX_W, PADBOX_W, C_BLACK);
  frame(PADBOX_X, PADBOX_Y, PADBOX_W, PADBOX_W, C_WHITE);
  {
    uint16_t cx = PADBOX_X + PADBOX_W / 2, cy = PADBOX_Y + PADBOX_W / 2;
    rect((uint16_t)(cx - 4), cy, 9, 1, C_GRAY);
    rect(cx, (uint16_t)(cy - 4), 1, 9, C_GRAY);
    uint16_t dx = (uint16_t)(PADBOX_X + 1 + ((uint32_t)px * (PADBOX_W - 3)) / 255);
    uint16_t dy = (uint16_t)(PADBOX_Y + 1 + ((uint32_t)py * (PADBOX_W - 3)) / 255);
    rect(dx, dy, 2, 2, C_WHITE);
  }
  g_display->flush();
}

uint8_t BIOS::paddlesScreen(bool redraw, int key)
{
  static uint8_t lastX = 0, lastY = 0;
  bool paint = redraw;

  uint8_t px = g_paddles->paddle0(), py = g_paddles->paddle1();
  if (px != lastX || py != lastY) {
    lastX = px;
    lastY = py;
    paint = true;
  }

  switch (key) {
  case NOKEY:
    break;
  case PK_ESC:
    return SCR_MAIN;
  case PK_UARR:
  case PK_DARR:
    sel = (uint8_t)(sel ? 0 : 1);
    paint = true;
    break;
  case PK_LARR:
  case PK_RARR:
  case PK_RET:
    if (sel == PD_X) g_invertPaddleX = !g_invertPaddleX;
    else             g_invertPaddleY = !g_invertPaddleY;
#ifdef TEENSYDUINO
    ((TeensyPaddles *)g_paddles)->setRev(g_invertPaddleX, g_invertPaddleY);
#endif
    paint = true;
    break;
  default:
    break;
  }

  if (paint) drawPaddles();
  return SCR_PADDLES;
}

// The "Net" tab. What it shows depends on the platform, because the two builds
// reach the network very differently:
//   Teensy: a real ESP-01 WiFi co-processor, so it needs the SSID/password and
//           a Connect action, and it reports live link status.
//   SDL:    rides the host's own network through a built-in user-mode NAT, so
//           there is no radio to configure (no SSID, password, or Connect); it
//           does have a port offset knob for exposing privileged ports.
// Common to both: the Uthernet card's slot, and the inbound forward-port list.
// Up/Down move between fields; on a text field typing edits it and DEL erases;
// on the Slot field a digit or +/- picks the slot. Return advances to the next
// field (or, on the Teensy Connect field, joins). Edits go straight into the
// globals (persisted to prefs on BIOS exit); the forward list is pushed to the
// live NAT when you leave this tab (see BIOS::loop). Changing the slot marks
// the card config changed, so the slots are reassigned when the VM resumes.
#ifdef TEENSYDUINO
enum { F_SLOT = 0, F_NET, F_SSID, F_PASS, F_FWD, F_CONNECT, F_COUNT };
#else
enum { F_SLOT = 0, F_NET, F_FWD, F_OFFSET, F_COUNT };
#endif

#ifdef TEENSYDUINO
static bool netRefresh = true;
static bool netConnecting = false;
static uint32_t netConnectStartMs = 0; // when "Connect" was pressed, to bound the wait
static int netStatus = 0;
static uint8_t netIp[4] = {0, 0, 0, 0};
// Co-processor firmware, read alongside the WiFi status so a stale/incompatible
// ESP is flagged. haveEspInfo is false until a successful CMD_GET_INFO.
static bool haveEspInfo = false;
static uint8_t espProto = 0, espFwMaj = 0, espFwMin = 0;
#endif

static void putField(uint8_t row, const char *value, bool editing)
{
  char buf[VWIDTH + 1];
  uint8_t room = (uint8_t)(editing ? VWIDTH - 1 : VWIDTH);
  size_t n = strlen(value);
  const char *from = (n > room) ? value + (n - room) : value;
  snprintf(buf, sizeof(buf), "%s%s", from, editing ? "_" : "");
  put(row, VCOL, buf, ST_NORMAL);
}

void BIOS::drawNetwork()
{
  char buf[64];

  clear(" NETWORK ");
  uint8_t y = 2;

  // --- the emulated card (both platforms) ---
  put(y, LCOL, "Uthernet II card", sel == F_SLOT ? ST_INV : ST_NORMAL);
  if (g_slotUthernet) {
    snprintf(buf, sizeof(buf), "slot %d", g_slotUthernet);
    put(y, VCOL, buf, ST_NORMAL);
  } else
    put(y, VCOL, "not installed", ST_DIM);
  y++;
  put(y++, 2, "Type digit to set slot; 0 removes it.", ST_DIM);
  y++;

  // --- the virtual LAN the NAT hands the Apple (both platforms) ---
  put(y, LCOL, "Subnet", sel == F_NET ? ST_INV : ST_NORMAL);
  putField(y, g_natSubnet, sel == F_NET);
  y++;
  {
    uint8_t net[4];
    if (unParseSubnet(g_natSubnet, net))
      put(y++, 2, "A /24: Apple .15, gateway .2, DNS .3.", ST_DIM);
    else
      put(y++, 2, "Enter a full address, e.g. 10.0.2.0.", ST_NORMAL);
  }
  y++;

#ifdef TEENSYDUINO
  // --- the WiFi radio it talks through (Teensy only) ---
  put(y, LCOL, "WiFi name", sel == F_SSID ? ST_INV : ST_NORMAL);
  putField(y, g_wifiSSID, sel == F_SSID);
  y++;
  put(y, LCOL, "WiFi password", sel == F_PASS ? ST_INV : ST_NORMAL);
  putField(y, g_wifiPass, sel == F_PASS);
  y++;
  y++;
#endif

  // --- inbound: let the outside reach a server on the Apple (both) ---
  put(y, LCOL, "Forward ports", sel == F_FWD ? ST_INV : ST_NORMAL);
  putField(y, g_natFwd, sel == F_FWD);
  y++;
  put(y++, 2, "Apple ports to listen, comma-separated", ST_DIM);
  put(y++, 2, "e.g. 80,23. Use 3 or fewer. 4 max.", ST_DIM);
  {
    // Each forward permanently holds one of the ESP's four sockets, and every
    // outbound flow needs a free one, so surface the budget as it gets tight
    // rather than silently dropping ports (the NAT only opens the first four).
    int nports = 0;
    for (const char *p = g_natFwd; *p; ) {
      if (*p >= '0' && *p <= '9') { nports++; while (*p >= '0' && *p <= '9') p++; }
      else p++;
    }
    if (nports > 4)
      put(y++, 2, "Too many: only the first 4 open.", ST_NORMAL);
#ifdef TEENSYDUINO
    else if (nports == 4)
      put(y++, 2, "4 ports leaves no socket for outbound.", ST_NORMAL);
    else if (nports == 3)
      put(y++, 2, "3 ports leave 1 outbound flow at once.", ST_NORMAL);
    else
      put(y++, 2, "Each uses 1 of the ESP's 4 sockets.", ST_DIM);
#endif
  }
  y++;

#ifndef TEENSYDUINO
  put(y, LCOL, "Port offset", sel == F_OFFSET ? ST_INV : ST_NORMAL);
  snprintf(buf, sizeof(buf), "%u", (unsigned)g_natPortOffset);
  putField(y, buf, sel == F_OFFSET);
  y++;
  put(y++, 2, "Added to ports below 1024 on the host,", ST_DIM);
  put(y++, 2, "so no root is needed to open them.", ST_DIM);
  y++;
  // --- desktop note (SDL): no radio to configure ---
  put(y++, 2, "The desktop uses the host's own", ST_DIM);
  put(y++, 2, "network connection.", ST_DIM);
#else
  // --- action + live ESP/WiFi status (Teensy only) ---
  put(y++, LCOL, "Connect to WiFi", sel == F_CONNECT ? ST_INV : ST_NORMAL);
  y++;

  if (netStatus < 0)       strcpy(buf, "Status: card off (set a slot above)");
  else if (netStatus == 0) strcpy(buf, "Status: ESP-01 not responding");
  else if (netStatus == 2) snprintf(buf, sizeof(buf), "Status: connected  %d.%d.%d.%d",
                                    netIp[0], netIp[1], netIp[2], netIp[3]);
  else if (netConnecting)  strcpy(buf, "Status: connecting...");
  else                     strcpy(buf, "Status: not joined (check password?)");
  put(y++, 2, buf, ST_NORMAL);

  // ESP firmware version, with a warning when it can't talk to this build. A
  // different wire protocol is a hard mismatch (nothing will work until the
  // ESP is reflashed); an older firmware still works but should be updated.
  if (haveEspInfo) {
    bool protoBad = (espProto != AIIE_ESP_PROTO_VERSION);
    bool fwOld    = (!protoBad &&
                     (espFwMaj <  AIIE_ESP_FW_MAJOR ||
                      (espFwMaj == AIIE_ESP_FW_MAJOR && espFwMin < AIIE_ESP_FW_MINOR)));
    if (protoBad)
      snprintf(buf, sizeof(buf), "ESP fw v%d.%d: REFLASH (proto %d, need %d)",
               espFwMaj, espFwMin, espProto, AIIE_ESP_PROTO_VERSION);
    else if (fwOld)
      snprintf(buf, sizeof(buf), "ESP fw v%d.%d: update to v%d.%d",
               espFwMaj, espFwMin, AIIE_ESP_FW_MAJOR, AIIE_ESP_FW_MINOR);
    else
      snprintf(buf, sizeof(buf), "ESP fw v%d.%d (proto %d)",
               espFwMaj, espFwMin, espProto);
    put(y++, 2, buf, (protoBad || fwOld) ? ST_NORMAL : ST_DIM);
  }

  // Live link counters, to see which side is dead: TX = commands sent to the
  // ESP, RX = valid replies, err = bytes that arrived but failed CRC.
  if (g_uthernet && netStatus >= 0) {
    snprintf(buf, sizeof(buf), "Link: TX %lu  RX %lu  err %lu",
             (unsigned long)g_uthernet->statFramesSent(),
             (unsigned long)g_uthernet->statFramesReceived(),
             (unsigned long)g_uthernet->statCrcErrors());
    put(y++, 2, buf, ST_DIM);
  }
#endif

  put(21, 2, "Up/Down move, type to edit, Del erases", ST_DIM);
  put(22, 2, "Esc returns.", ST_DIM);
  g_display->flush();
}

uint8_t BIOS::networkScreen(bool redraw, int key)
{
  bool paint = redraw;

  if (redraw) {
#ifdef TEENSYDUINO
    netRefresh = true;
#endif
  }

  switch (key) {
  case NOKEY:
    break;
  case PK_ESC:
    return SCR_MAIN;
  case PK_UARR:
    sel = (uint8_t)(sel ? sel - 1 : F_COUNT - 1);
    paint = true;
    break;
  case PK_DARR:
    sel = (uint8_t)((sel + 1) % F_COUNT);
    paint = true;
    break;
  case PK_RET:
#ifdef TEENSYDUINO
    if (sel == F_CONNECT) {
      if (g_uthernet) {
        splash("Connecting...");
        g_uthernet->wifiJoin(g_wifiSSID, g_wifiPass);
      }
      netConnecting = true;
      netConnectStartMs = millis();
      netRefresh = true;
      paint = true;
      break;
    }
#endif
    sel = (uint8_t)((sel + 1) % F_COUNT);   // Return advances to the next field
    paint = true;
    break;
  default:
    break;
  }

  // --- per-field key handling ----------------------------------------------
  if (sel == F_SLOT && key != NOKEY) {
    // Assign the Uthernet card's slot here (same effect as the Cards menu): a
    // digit picks a slot directly, +/= steps forward, - steps back, 0 disables.
    int step = 0;
    if (key >= '0' && key <= '9') {
      if (placeCard(NCARDS - 1, (uint8_t)(key - '0'))) paint = true;
    }
    else if (key == PK_RARR || key == '+' || key == '=') step = 1;
    else if (key == PK_LARR || key == '-')               step = -1;
    if (step) {
      stepCard(NCARDS - 1, step);
      paint = true;
    }
  }

  {
    // Text entry: the forward-port list on both platforms, plus SSID/Password on
    // the Teensy.
    char *tfield = NULL; size_t tcap = 0;
    bool restrictFwd = false, restrictIp = false;
    if (sel == F_FWD)      { tfield = g_natFwd;    tcap = sizeof(g_natFwd) - 1;    restrictFwd = true; }
    else if (sel == F_NET) { tfield = g_natSubnet; tcap = sizeof(g_natSubnet) - 1; restrictIp = true; }
#ifdef TEENSYDUINO
    else if (sel == F_SSID) { tfield = g_wifiSSID; tcap = 32; }
    else if (sel == F_PASS) { tfield = g_wifiPass; tcap = 63; }
#endif
    if (tfield && key != NOKEY) {
      size_t len = strlen(tfield);
      bool accept;
      if (restrictIp)       accept = (key >= '0' && key <= '9') || key == '.';
      else if (restrictFwd) accept = (key >= '0' && key <= '9') || key == ',' || key == ' ';
      else                  accept = true;
      if (key == PK_DEL) {
        if (len > 0) { tfield[len - 1] = 0; paint = true; }
      } else if (key >= 0x20 && key <= 0x7E && accept) {
        if (len < tcap) { tfield[len] = (char)key; tfield[len + 1] = 0; paint = true; }
      }
    }
  }

#ifndef TEENSYDUINO
  // The SDL-only port offset is a number: digits append, DEL trims a digit.
  if (sel == F_OFFSET && key != NOKEY) {
    if (key == PK_DEL) { g_natPortOffset /= 10; paint = true; }
    else if (key >= '0' && key <= '9') {
      uint32_t v = (uint32_t)g_natPortOffset * 10 + (uint32_t)(key - '0');
      g_natPortOffset = (v > 65535) ? 65535 : (uint16_t)v;
      paint = true;
    }
  }
#endif

#ifdef TEENSYDUINO
  // Poll the ESP link about once a second so the status line and counters update
  // live, turning this tab into a link tester. This handler runs on every BIOS
  // loop tick whether or not a key was pressed; the ESP probe itself is throttled
  // inside the backend.
  static uint8_t refreshTick = 0;
  if (++refreshTick >= 10) {
    refreshTick = 0;
    netRefresh = true;
    paint = true;
  }
  if (paint && netRefresh) {
    if (g_uthernet) netStatus = g_uthernet->wifiStatus(netIp);
    else            netStatus = -1;
    // With the link up, read the ESP firmware so the version line below can flag
    // a mismatch; drop it when the link is down so a stale readout can't linger.
    haveEspInfo = (g_uthernet && netStatus >= 1 &&
                   g_uthernet->espInfo(espProto, espFwMaj, espFwMin));
    netRefresh = false;
    // Leave the "connecting..." state once we're up, or give up after a while
    // so a genuinely bad password eventually reads as failed rather than
    // spinning forever.
    if (netStatus == 2 || (uint32_t)(millis() - netConnectStartMs) > 20000)
      netConnecting = false;
  }
#endif

  if (paint) drawNetwork();
  return SCR_NETWORK;
}

enum {
  AD_MONITOR = 0,
  AD_DEBUG,
  AD_SUSPEND,
  AD_RESTORE,
#ifdef TEENSYDUINO
  AD_UPDATEFW,
#endif
  NADVANCED
};

void BIOS::drawAdvanced()
{
  clear(" ADVANCED ");
  uint8_t y = 3;
  put(y, LCOL, "Drop to Monitor", sel == AD_MONITOR ? ST_INV : ST_NORMAL);
  y++;
  put(y, LCOL, "Debug overlay", sel == AD_DEBUG ? ST_INV : ST_NORMAL);
  put(y, VCOL, debugModeName(g_debugMode), ST_NORMAL);
  y++;
  put(y, LCOL, "Save a snapshot", sel == AD_SUSPEND ? ST_INV : ST_NORMAL);
  put(y, VCOL, "to suspend.vm", ST_DIM);
  y++;
  put(y, LCOL, "Restore the snapshot", sel == AD_RESTORE ? ST_INV : ST_NORMAL);
  y++;
#ifdef TEENSYDUINO
  put(y, LCOL, "Update firmware", sel == AD_UPDATEFW ? ST_INV : ST_NORMAL);
  put(y, VCOL, "from /AIIE.HEX", ST_DIM);
  y++;
#endif

  put(11, 2, "Dropping to Monitor is the //e native", ST_DIM);
  put(12, 2, "debugger at $FF69, live, no reboot.", ST_DIM);
  put(14, 2, "Some debugging info can be shown in", ST_DIM);
  put(15, 2, "the bottom-left corner.  L/R pick.", ST_DIM);
  put(17, 2, "A snapshot is the whole machine, saved", ST_DIM);
  put(18, 2, "to disk for restore later.", ST_DIM);

  put(21, 2, "Up/Down move, Return select, Esc back", ST_DIM);
  g_display->flush();
}

uint8_t BIOS::advancedScreen(bool redraw, int key)
{
  bool paint = redraw;

  switch (key) {
  case NOKEY:
    break;
  case PK_ESC:
    return SCR_MAIN;
  case PK_UARR:
    sel = (uint8_t)(sel ? sel - 1 : NADVANCED - 1);
    paint = true;
    break;
  case PK_DARR:
    sel = (uint8_t)((sel + 1) % NADVANCED);
    paint = true;
    break;
  case PK_LARR:
  case PK_RARR:
    if (sel == AD_DEBUG) {
      g_debugMode = (uint8_t)((g_debugMode + (key == PK_RARR ? 1 : NDEBUGMODES - 1)) % NDEBUGMODES);
      paint = true;
    }
    break;
  case PK_RET:
    switch (sel) {
    case AD_MONITOR:
      leaveBIOS();
      ((AppleVM *)g_vm)->Monitor();
      return SCR_DONE;
    case AD_DEBUG:
      g_debugMode = (uint8_t)((g_debugMode + 1) % NDEBUGMODES);
      paint = true;
      break;
    case AD_SUSPEND:
      splash("Saving the machine...");
      // CPU is already suspended, so this is safe...
      ((AppleVM *)g_vm)->Suspend("suspend.vm");
      paint = true;
      break;
    case AD_RESTORE:
      splash("Restoring the machine...");
      ((AppleVM *)g_vm)->Resume("suspend.vm");
      return SCR_DONE;
#ifdef TEENSYDUINO
    case AD_UPDATEFW:
      // Reflash from /AIIE.HEX on the SD card. On success this reboots into
      // the new firmware and never returns; on failure (missing/invalid
      // file) it leaves the machine untouched and we redraw the menu.
      teensySelfUpdateFromSD("/AIIE.HEX");
      paint = true;
      break;
#endif
    }
    break;
  default:
    break;
  }

  if (paint) drawAdvanced();
  return SCR_ADVANCED;
}

#define ABOUT_BOXROW 4
#define ABOUT_BOXCOL 2
#define ABOUT_W 36
#define ABOUT_H 18

static const char aboutText[] =
  "                                    "
  "                                    "
  "  ... an Apple //e emulator         "
  "      written by                    "
  "        Jorj Bauer <jorj@jorj.org>  "
  "                                    "
  "                                    "
  "  (c) 2017-2026 Jorj Bauer          "
  "                                    "
  "                                    "
  "  Source code is available at       "
  "        github.com/JorjBauer/aiie/  "
  "                                    "
  "                                    "
  "                                    "
  "                                    "
  "  Press Return... "; // intentionally short so cursor stays here

uint8_t BIOS::aboutScreen(bool redraw, int key)
{
  static uint16_t ptr = 0;
  static bool cursorOn = false;
  static uint8_t blink = 0;
  const size_t len = strlen(aboutText);

  if (redraw) {
    clear(" ABOUT ");
    putCentered(2, "Aiie!", ST_NORMAL);
    for (uint8_t r = 0; r < ABOUT_H; r++)
      for (uint8_t c = 0; c < ABOUT_W; c++)
        cell((uint8_t)(ABOUT_BOXROW + r), (uint8_t)(ABOUT_BOXCOL + c), ' ', ST_PLAIN);
    ptr = 0;
    cursorOn = false;
    blink = 0;
  }

  if (key == PK_ESC || key == PK_RET)
    return SCR_MAIN;

  if (ptr < len) {
    // Draw the next character
    bool didOne = false;
    while (ptr < len && (!didOne || ptr < ABOUT_W)) {
      char c = aboutText[ptr];
      uint8_t r = (uint8_t)(ABOUT_BOXROW + ptr / ABOUT_W);
      uint8_t col = (uint8_t)(ABOUT_BOXCOL + ptr % ABOUT_W);
      if (c != ' ') {
        cell(r, col, (unsigned char)c, ST_PLAIN);
        didOne = true;
      }
      ptr++;
    }
  } else {
    // Flash the cursor until the user exits
    if (++blink >= 8) {
      blink = 0;
      cursorOn = !cursorOn;
    }
    uint8_t r = (uint8_t)(ABOUT_BOXROW + len / ABOUT_W);
    uint8_t col = (uint8_t)(ABOUT_BOXCOL + len % ABOUT_W);
    cell(r, col, cursorOn ? 0x8E : ' ', ST_PLAIN);
  }
  g_display->flush();

  return SCR_ABOUT;
}

// Read a directory, cache all the entries
void BIOS::cacheDirectory()
{
  const char *filter = browseFilter(browseKind);
  // If we've already cached this directory, then just return it
  if (numCacheEntries && !strcmp(cachedPath, rootPath) && !strcmp(cachedFilter, filter))
    return;

  // Otherwise flush the cache and start over
  numCacheEntries = 0;
  strncpy(cachedPath, rootPath, sizeof(cachedPath) - 1);
  cachedPath[sizeof(cachedPath) - 1] = 0;
  strncpy(cachedFilter, filter, sizeof(cachedFilter) - 1);
  cachedFilter[sizeof(cachedFilter) - 1] = 0;

  // This could be a lengthy process, so...
  splash("Reading the folder...");

  // read all the entries we can find
  int16_t idx = 0;
  while (numCacheEntries < BIOSCACHESIZE) {
    struct _cacheEntry *ce = &biosCache[numCacheEntries];
    idx = g_filemanager->readDir(rootPath, filter, ce->fn, idx, BIOS_MAXPATH);
    if (idx == -1) break;
    idx++;
    numCacheEntries++;
  }

  qsort(biosCache, numCacheEntries, sizeof(biosCache[0]), compareEntries);
}

void BIOS::refilter()
{
  numVisible = 0;
  for (uint16_t i = 0; i < numCacheEntries; i++) {
    if (isDirEntry(biosCache[i].fn) || nameMatches(biosCache[i].fn, findText))
      visible[numVisible++] = i;
  }
  browseTop = browseSel = 0;
}

void BIOS::openBrowser(uint8_t kind, uint8_t drive)
{
  browseKind = kind;
  browseDrive = drive;
  findText[0] = 0;
  cacheDirectory();
  refilter();
}

void BIOS::enterDirectory(const char *name)
{
  if (strlen(rootPath) + strlen(name) >= sizeof(rootPath)) return;
  strcat(rootPath, name);
  findText[0] = 0;
  cacheDirectory();
  refilter();
}

void BIOS::stripDirectory()
{
  rootPath[strlen(rootPath) - 1] = '\0'; // remove the last character
  while (rootPath[0] && rootPath[strlen(rootPath) - 1] != '/')
    rootPath[strlen(rootPath) - 1] = '\0'; // remove the last character again

  // We're either at the previous directory, or we've nulled out the whole thing.
  if (rootPath[0] == '\0')
    // Never go beyond this
    strcpy(rootPath, "/");
  findText[0] = 0;
  cacheDirectory();
  refilter();
}

static bool browseDriveLoaded(uint8_t kind, uint8_t drive)
{
  const char *name = (kind == KIND_HD) ? ((AppleVM *)g_vm)->HDName(drive)
    : ((AppleVM *)g_vm)->DiskName(drive);
  return name && name[0];
}

static uint16_t listCount(bool ejectRow)
{
  return (uint16_t)(numVisible + (ejectRow ? 1 : 0));
}

static int32_t listEntry(uint16_t i, bool ejectRow)
{
  if (ejectRow) {
    if (i == 0) return -1;
    i--;
  }
  return (i < numVisible) ? visible[i] : -1;
}

void BIOS::drawBrowser()
{
  char buf[BIOS_MAXPATH + 8];
  bool ejectRow = browseDriveLoaded(browseKind, browseDrive);
  uint16_t count = listCount(ejectRow);

  clear(browseKind == KIND_FD ? " SELECT A DISKETTE " : " SELECT A HARD DISK ");

  {
    size_t n = strlen(rootPath);
    const char *from = (n > COLS - 2) ? rootPath + (n - (COLS - 2)) : rootPath;
    put(1, 1, from, ST_DIM);
  }

  for (uint8_t i = 0; i < PAGESZ; i++) {
    uint16_t idx = (uint16_t)(browseTop + i);
    if (idx >= count) break;
    uint8_t row = (uint8_t)(LISTTOP + i);
    bool selected = (idx == browseSel);
    int32_t e = listEntry(idx, ejectRow);
    if (e < 0) {
      const char *name = (browseKind == KIND_HD) ? ((AppleVM *)g_vm)->HDName(browseDrive)
        : ((AppleVM *)g_vm)->DiskName(browseDrive);
      snprintf(buf, sizeof(buf), "Eject %s", baseName(name));
      putName(row, 4, buf, NAMEWIDTH, selected ? ST_INV : ST_NORMAL);
      continue;
    }
    const char *fn = biosCache[e].fn;
    if (isDirEntry(fn)) {
      put(row, 1, "\x98\x99", ST_NORMAL);
      snprintf(buf, sizeof(buf), "%.*s", (int)(strlen(fn) - 1), fn);
      putName(row, 4, buf, NAMEWIDTH, selected ? ST_INV : ST_NORMAL);
    } else {
      putName(row, 4, fn, NAMEWIDTH, selected ? ST_INV : ST_NORMAL);
    }
  }

  if (browseTop > 0)
    put(LISTTOP, COLS - 1, A_UP, ST_NORMAL);
  if (browseTop + PAGESZ < count)
    put(LISTTOP + PAGESZ - 1, COLS - 1, A_DN, ST_NORMAL);

  if (count == 0) {
    put(LISTTOP + 1, 3, findText[0] ? "Nothing matches." : "Nothing to show.", ST_DIM);
  } else {
    uint16_t last = (uint16_t)(browseTop + PAGESZ);
    if (last > count) last = count;
    snprintf(buf, sizeof(buf), "%u-%u of %u", (unsigned)(browseTop + 1), (unsigned)last, (unsigned)count);
    put(19, 2, buf, ST_NORMAL);
  }

  put(20, 2, "Find:", ST_NORMAL);
  if (findText[0])
    put(20, 8, findText, ST_INV);
  else
    put(20, 8, "(type to search)", ST_DIM);

  put(21, 2, "Up/Down move, L/R page, Return opens.", ST_DIM);
  put(22, 2, findText[0] ? "Esc clears the search." : "Esc returns.", ST_DIM);
  g_display->flush();
}

static void insertImage(uint8_t kind, uint8_t drive, const char *path, const char *fn)
{
  if (kind == KIND_HD)
    ((AppleVM *)g_vm)->insertHD(drive, staticPathConcat(path, fn));
  else
    // drawIt is false b/c we don't want to draw it immediately; that
    // would draw over the bios screen
    ((AppleVM *)g_vm)->insertDisk(drive, staticPathConcat(path, fn), false);
}

uint8_t BIOS::browseScreen(bool redraw, int key)
{
  bool paint = redraw;
  bool ejectRow = browseDriveLoaded(browseKind, browseDrive);
  uint16_t count = listCount(ejectRow);

  switch (key) {
  case NOKEY:
    break;
  case PK_ESC:
    if (findText[0]) {
      findText[0] = 0;
      refilter();
      paint = true;
      break;
    }
    return SCR_MAIN;
  case PK_UARR:
    if (browseSel > 0) browseSel--;
    paint = true;
    break;
  case PK_DARR:
    if (browseSel + 1 < count) browseSel++;
    paint = true;
    break;
  case PK_LARR:
    browseSel = (browseSel >= PAGESZ) ? (uint16_t)(browseSel - PAGESZ) : 0;
    paint = true;
    break;
  case PK_RARR:
    if (count) {
      browseSel = (uint16_t)(browseSel + PAGESZ);
      if (browseSel >= count) browseSel = (uint16_t)(count - 1);
    }
    paint = true;
    break;
  case PK_DEL:
    {
      size_t n = strlen(findText);
      if (n) {
        findText[n - 1] = 0;
        refilter();
        paint = true;
      }
    }
    break;
  case PK_RET:
    {
      if (!count) break;
      int32_t e = listEntry(browseSel, ejectRow);
      if (e < 0) {
        if (browseKind == KIND_HD) {
          ((AppleVM *)g_vm)->ejectHD(browseDrive);
          hdChanged = true;
          mainSel = IT_REBOOT;
        } else
          ((AppleVM *)g_vm)->ejectDisk(browseDrive);
        return SCR_MAIN;
      }
      const char *fn = biosCache[e].fn;
      if (!strcmp(fn, "../")) {
        // Go up a directory (strip a directory name from rootPath)
        stripDirectory();
        paint = true;
        break;
      }
      if (isDirEntry(fn)) {
        // Descend in to the directory. FIXME: file path length?
        enterDirectory(fn);
        paint = true;
        break;
      }
      insertImage(browseKind, browseDrive, rootPath, fn);
      if (browseKind == KIND_HD) {
        hdChanged = true;
        mainSel = IT_REBOOT;
      }
      return SCR_MAIN;
    }
  default:
    if (key >= 0x20 && key < 0x7F) {
      size_t n = strlen(findText);
      if (n < sizeof(findText) - 1) {
        findText[n] = (char)key;
        findText[n + 1] = 0;
        refilter();
        paint = true;
      }
    }
    break;
  }

  if (browseSel < browseTop)
    browseTop = browseSel;
  else if (browseSel >= browseTop + PAGESZ)
    browseTop = (uint16_t)(browseSel - PAGESZ + 1);

  if (paint) drawBrowser();
  return SCR_BROWSE;
}

uint8_t BIOS::notice(const char *a, const char *b, uint8_t back)
{
  noticeA = a;
  noticeB = b;
  noticeBack = back;
  return SCR_NOTICE;
}

uint8_t BIOS::messageScreen(bool redraw, int key)
{
  if (redraw) {
    clear(" AIIE ");
    put(8, 3, noticeA, ST_NORMAL);
    if (noticeB) put(10, 3, noticeB, ST_NORMAL);
    put(21, 2, "Press any key.", ST_DIM);
    g_display->flush();
  }
  if (key != NOKEY)
    return noticeBack;
  return SCR_NOTICE;
}

void BIOS::WarmReset()
{
  g_cpu->Reset();
  g_vm->busReset();
}

void BIOS::RebootAsIs()
{
  // g_vm->Reset() will eject disks. We don't want to do that, so we need to
  // grab the inserted disk names; reset the VM; then restore the disks.
  char *disk6s1 = strdup(((AppleVM *)g_vm)->DiskName(0) ? ((AppleVM *)g_vm)->DiskName(0) : "");
  char *disk6s2 = strdup(((AppleVM *)g_vm)->DiskName(1) ? ((AppleVM *)g_vm)->DiskName(1) : "");
  char *hdd1 = strdup(((AppleVM *)g_vm)->HDName(0) ? ((AppleVM *)g_vm)->HDName(0) : "");
  char *hdd2 = strdup(((AppleVM *)g_vm)->HDName(1) ? ((AppleVM *)g_vm)->HDName(1) : "");

  g_vm->Reset();
  g_cpu->Reset();

  if (disk6s1[0])
    ((AppleVM *)g_vm)->insertDisk(0, disk6s1);
  if (disk6s2[0])
    ((AppleVM *)g_vm)->insertDisk(1, disk6s2);
  if (hdd1[0])
    ((AppleVM *)g_vm)->insertHD(0, hdd1);
  if (hdd2[0])
    ((AppleVM *)g_vm)->insertHD(1, hdd2);   // HD32 has drives 0 and 1; 2 was out of bounds

  free(disk6s1);
  free(disk6s2);
  free(hdd1);
  free(hdd2);
}

void BIOS::ColdReboot()
{
  g_vm->Reset();
  g_cpu->Reset();
}
