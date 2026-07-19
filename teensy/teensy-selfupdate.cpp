// Teensy 4.1 firmware self-update from an SD-card Intel-HEX image.
//
// The dangerous flash erase/write/move primitives live in the vendored,
// public-domain FlashTxx.c/.h (Niels Moseley / Jon Zeeff / Frank Boesing /
// Joe Pasquariello -- github.com/joepasquariello/FlasherX). This file
// supplies a NON-interactive driver suited to being launched from the BIOS:
// it streams the hex from SD, shows progress on the display, aborts safely
// before the point of no return, and never prompts over Serial.
//
// The Intel-HEX line parser (parse_hex_line / process_hex_record) is copied
// verbatim from FlasherX's FXUtil.cpp, which is itself public domain
// (Paul Stoffregen's hex reader, with type tweaks by Jon Zeeff).

#include <Arduino.h>
#include <stdio.h>                // sscanf, snprintf
#include <string.h>              // strlen

#include "teensy-filemanager.h"   // SdFat, FsFile, TeensyFileManager::getSdFat()
#include "globals.h"              // g_display, g_filemanager, g_keyboard, M_NORMAL
#include "appledisplay.h"         // c_darkblue
#include "teensy-selfupdate.h"
#include "teensy-fwversion.h"     // g_fwVersion, AIIE_FW_MAGIC
#include "physicalkeyboard.h"     // PK_RET, PK_ESC
#include "teensy-usb.h"           // teensyServiceInput()

extern "C" {
  #include "FlashTxx.h"           // firmware_buffer_init/flash_write_block/...
}

// Display layout (mirrors bios.cpp, whose macros aren't exported).
#define UI_INDENT     10
#define UI_LINEHEIGHT 10

//******************************************************************************
// Intel-HEX record bookkeeping (verbatim from FlasherX FXUtil.cpp; public
// domain). 'data' is sized to hold a full 255-byte record defensively, even
// though Teensy hex files only ever use 32-byte records.
//******************************************************************************
typedef struct {
  char *data;           // pointer to array allocated elsewhere
  unsigned int addr;    // address in intel hex record
  unsigned int code;    // intel hex record type (0=data, etc.)
  unsigned int num;     // number of data bytes in intel hex record
  uint32_t base;        // base address to be added to intel hex 16-bit addr
  uint32_t min;         // min address in hex file
  uint32_t max;         // max address in hex file
  int eof;              // set true on intel hex EOF (code = 1)
  int lines;            // number of hex records received
} hex_info_t;

// process record and return okay (0) or error (1) -- verbatim from FXUtil.cpp
static int process_hex_record( hex_info_t *hex )
{
  if (hex->code==0) { // data -- update min/max address so far
    if (hex->base + hex->addr + hex->num > hex->max)
      hex->max = hex->base + hex->addr + hex->num;
    if (hex->base + hex->addr < hex->min)
      hex->min = hex->base + hex->addr;
  }
  else if (hex->code==1) { // EOF (:flash command not received yet)
    hex->eof = 1;
  }
  else if (hex->code==2) { // extended segment address (top 16 of 24-bit addr)
    hex->base = ((hex->data[0] << 8) | hex->data[1]) << 4;
  }
  else if (hex->code==3) { // start segment address (80x86 real mode only)
    return 1;
  }
  else if (hex->code==4) { // extended linear address (top 16 of 32-bit addr)
    hex->base = ((hex->data[0] << 8) | hex->data[1]) << 16;
  }
  else if (hex->code==5) { // start linear address (32-bit big endian addr)
    hex->base = (hex->data[0] << 24) | (hex->data[1] << 16)
              | (hex->data[2] <<  8) | (hex->data[3] <<  0);
  }
  else {
    return 1;
  }

  return 0;
}

// parse a line of intel hex; returns 1 if valid, 0 on error -- verbatim from
// FXUtil.cpp (Paul Stoffregen, public domain).
static int parse_hex_line( const char *theline, char *bytes,
		unsigned int *addr, unsigned int *num, unsigned int *code )
{
  unsigned sum, len, cksum;
  const char *ptr;
  int temp;

  *num = 0;
  if (theline[0] != ':')
    return 0;
  if (strlen (theline) < 11)
    return 0;
  ptr = theline + 1;
  if (!sscanf (ptr, "%02x", &len))
    return 0;
  ptr += 2;
  if (strlen (theline) < (11 + (len * 2)))
    return 0;
  if (!sscanf (ptr, "%04x", (unsigned int *)addr))
    return 0;
  ptr += 4;
  if (!sscanf (ptr, "%02x", code))
    return 0;
  ptr += 2;
  sum = (len & 255) + ((*addr >> 8) & 255) + (*addr & 255) + (*code & 255);
  while (*num != len)
  {
    if (!sscanf (ptr, "%02x", &temp))
      return 0;
    bytes[*num] = temp;
    ptr += 2;
    sum += bytes[*num] & 255;
    (*num)++;
    if (*num >= 256)
      return 0;
  }
  if (!sscanf (ptr, "%02x", &cksum))
    return 0;

  if (((sum & 255) + (cksum & 255)) & 255)
    return 0;     /* checksum error */
  return 1;
}

//******************************************************************************
// local helpers
//******************************************************************************

// Draw one status line, padded so it overwrites any previous (longer) text.
static void uiLine(int line, const char *str)
{
  char padded[40];
  snprintf(padded, sizeof(padded), "%-38s", str);
  g_display->drawString(M_NORMAL, UI_INDENT, 20 + UI_LINEHEIGHT * line, padded);
  g_display->flush();
}

// Draw a textual progress bar: "[##########          ]  42%"
static void drawProgressBar(int line, uint8_t pct)
{
  if (pct > 100) pct = 100;
  const int W = 24;                 // number of bar cells
  int filled = (pct * W) / 100;
  char bar[40];
  int n = 0;
  bar[n++] = '[';
  for (int c = 0; c < W; c++)
    bar[n++] = (c < filled) ? '#' : ' ';
  bar[n++] = ']';
  snprintf(bar + n, sizeof(bar) - n, " %3d%%", (int)pct);
  uiLine(line, bar);
}

// Read one line (terminated by CR/LF or EOF) from the file. Leading EOL
// characters are skipped. Returns the line length, or -1 at end of file.
static int readHexLine(FsFile &f, char *line, int maxbytes)
{
  int n = 0;
  int c;

  do { c = f.read(); } while (c == '\n' || c == '\r');
  if (c < 0) return -1; // EOF

  line[n++] = (char)c;
  while (n < maxbytes - 1) {
    c = f.read();
    if (c < 0 || c == '\n' || c == '\r')
      break;
    line[n++] = (char)c;
  }
  line[n] = '\0';
  return n;
}

// Search a buffered firmware image for the embedded version marker
// (AIIE_FW_MAGIC, see teensy-fwversion.h). Copies the version text between the
// magic and the next 0x1e into out (NUL-terminated). Returns true if found; an
// older image built before the marker existed simply won't have it.
static bool scanImageVersion(const uint8_t *img, uint32_t len,
                             char *out, int outLen)
{
  const char *magic = AIIE_FW_MAGIC;
  const int mlen = (int)strlen(magic);
  if (len < (uint32_t)mlen) return false;
  for (uint32_t i = 0; i + (uint32_t)mlen <= len; i++) {
    if (memcmp(img + i, magic, mlen) != 0) continue;
    const uint8_t *v = img + i + mlen;
    int j = 0;
    while (j < outLen - 1 && (v + j) < (img + len) && v[j] != 0x1e && v[j] != 0) {
      out[j] = (char)v[j];
      j++;
    }
    out[j] = '\0';
    return true;
  }
  return false;
}

//******************************************************************************
// teensySelfUpdateFromSD()
//******************************************************************************
bool teensySelfUpdateFromSD(const char *path)
{
  SdFat *sd = ((TeensyFileManager *)g_filemanager)->getSdFat();
  FsFile hexFile;
  char buf[40];

  g_display->clrScr(c_darkblue);
  uiLine(0, "Firmware update from SD");
  uiLine(2, "Reading and checking image...");

  if (!sd || !hexFile.open(path, O_RDONLY)) {
    snprintf(buf, sizeof(buf), "Not found: %s", path);
    uiLine(5, buf);
    delay(3000);
    return false;
  }

  // Carve a buffer out of the spare upper region of program flash.
  uint32_t buffer_addr, buffer_size;
  if (firmware_buffer_init(&buffer_addr, &buffer_size) == 0) {
    uiLine(5, "Cannot allocate flash buffer.");
    hexFile.close();
    delay(3000);
    return false;
  }

  static char line[96];
  static char data[256] __attribute__((aligned(8)));
  hex_info_t hex = { data, 0, 0, 0, 0, 0xFFFFFFFF, 0, 0, 0 };

  bool ok = true;
  const char *err = NULL;

  // Progress is tracked against the hex file's byte position. We redraw the
  // bar only when the whole-number percent changes, to keep display refreshes
  // from slowing the (SD-read-bound) buffering loop.
  uint64_t totalBytes = hexFile.fileSize();
  uint8_t lastPct = 255; // force an initial draw at 0%
  uiLine(3, "Buffering image:");
  drawProgressBar(4, 0);

  while (!hex.eof) {
    if (readHexLine(hexFile, line, sizeof(line)) < 0) {
      err = "Unexpected end of hex file."; ok = false; break;
    }
    if (parse_hex_line(line, hex.data, &hex.addr, &hex.num, &hex.code) == 0) {
      err = "Corrupt hex line - aborting."; ok = false; break;
    }
    if (process_hex_record(&hex) != 0) {
      err = "Invalid hex record - aborting."; ok = false; break;
    }

    if (hex.code == 0) { // data record -> stash into the buffer
      uint32_t addr = buffer_addr + hex.base + hex.addr - FLASH_BASE_ADDR;
      if (hex.max > (FLASH_BASE_ADDR + buffer_size)) {
        err = "Image too large - aborting."; ok = false; break;
      }
      if (!IN_FLASH(buffer_addr)) {
        memcpy((void *)addr, (void *)hex.data, hex.num);
      }
      else if (flash_write_block(addr, hex.data, hex.num) != 0) {
        err = "Flash buffer write error."; ok = false; break;
      }
    }

    hex.lines++;
    if (totalBytes) {
      uint8_t pct = (uint8_t)((hexFile.curPosition() * 100) / totalBytes);
      if (pct != lastPct) {
        drawProgressBar(4, pct);
        lastPct = pct;
      }
    }
  }

  hexFile.close();

  // Verify the buffered image is actually built for this board before we
  // commit. check_flash_id() looks for the FLASH_ID string ("fw_teensy41")
  // which is embedded in every build that vendors FlashTxx.
  if (ok && !check_flash_id(buffer_addr, hex.max - hex.min)) {
    err = "Not a Teensy 4.1 image - aborting."; ok = false;
  }

  if (!ok) {
    firmware_buffer_free(buffer_addr, buffer_size);
    uiLine(5, err ? err : "Update aborted.");
    uiLine(6, "Firmware unchanged.");
    delay(4000);
    return false;
  }

  // Confirmation. The image is fully buffered and verified as a Teensy 4.1
  // build, but nothing irreversible has happened yet -- the running program is
  // untouched until flash_move() below -- so cancelling here is completely
  // safe. Show installed-vs-new version and the time/power warning, and require
  // an explicit keypress before crossing the point of no return.
  {
    char newVer[48];
    if (!scanImageVersion((const uint8_t *)buffer_addr, hex.max - hex.min,
                          newVer, sizeof(newVer)))
      strcpy(newVer, "unknown (pre-versioning build)");

    g_display->clrScr(c_darkblue);
    uiLine(0, "Firmware update from SD");
    snprintf(buf, sizeof(buf), "Installed: %s", g_fwVersion);
    uiLine(2, buf);
    snprintf(buf, sizeof(buf), "New image: %s", newVer);
    uiLine(3, buf);
    uiLine(5, "Update takes about 2 minutes.");
    uiLine(6, "Keep power connected the whole time -");
    uiLine(7, "a power loss mid-flash bricks it until");
    uiLine(8, "you re-flash over USB.");
    uiLine(10, "[Return] update     [ESC] cancel");
    g_display->flush();

    for (;;) {
      if (g_keyboard->kbhit()) {
        int8_t k = g_keyboard->read();
        if (k == PK_RET) break;          // confirmed -> fall through to flash
        if (k == PK_ESC) {
          firmware_buffer_free(buffer_addr, buffer_size);
          g_display->clrScr(c_darkblue);
          uiLine(0, "Firmware update from SD");
          uiLine(3, "Cancelled. Firmware unchanged.");
          g_display->flush();
          delay(2500);
          return false;
        }
      }
      teensyServiceInput();  // keep pumping USB/keyboard while we block here
      yield();
    }
    g_display->clrScr(c_darkblue);
    uiLine(0, "Firmware update from SD");
  }

  drawProgressBar(4, 100); // image fully buffered
  snprintf(buf, sizeof(buf), "Writing %lu bytes to flash...",
           (unsigned long)(hex.max - hex.min));
  uiLine(5, buf);
  // The flash erase/write below runs entirely from RAM with interrupts off
  // and overwrites the program itself (including the display driver), so the
  // screen cannot update while it runs -- it'll appear frozen for up to a
  // minute. flash_move() blinks the on-board LED per sector as a liveness
  // heartbeat instead; pin 13 must be a GPIO output for that to show.
  uiLine(6, "Screen freezes ~1 min; LED blinks.");
  uiLine(7, "DO NOT POWER OFF - auto-reboots.");
  pinMode(LED_BUILTIN, OUTPUT);
  delay(750); // let the final display update settle before we erase ourselves

  // Point of no return: move the new firmware from the buffer down into
  // program flash and reboot. flash_move() runs from RAM and does not return.
  flash_move(FLASH_BASE_ADDR, buffer_addr, hex.max - hex.min);

  REBOOT; // not reached
  return true;
}
