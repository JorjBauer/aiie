#include "debugger.h"
#include "globals.h"
#include "disassembler.h"
#include "applekeyboard.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>

#include <string.h>

Disassembler dis;

static void *cpu_thread(void *objptr) {
  Debugger *obj = (Debugger *)objptr;

  while (1) {
    struct sockaddr_in client;
    socklen_t clilen = sizeof(client);
    
    int newsockfd = accept(obj->sd, (struct sockaddr *)&client, &clilen);
    
    if (newsockfd < 0) {
      perror("ERROR on accept");
      exit(1);
    }
    
    obj->setSocket(newsockfd);
    
    sleep(1);
  }
}

Debugger::Debugger()
{
  cd = -1;
  removeAllBreakpoints();

  history = NULL;
  endh = NULL;
  historyCount = 0;

  steppingOut = false;
  singleStep = false;

#ifndef __EMSCRIPTEN__
  struct sockaddr_in server;
  int optval;

  sd = socket(AF_INET, SOCK_STREAM, 0);

  optval=1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR,
	     (void*)&optval, sizeof(optval));

  memset(&server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(12345);

  if (bind(sd, (struct sockaddr *) &server, sizeof(server)) < 0) {
    perror("error binding to debug socket");
    exit(1);
  }

  listen(sd,5);

  if (!pthread_create(&listenThreadID, NULL, &cpu_thread, (void *)this)) {
    ; // ... what?
  }
#else
  sd = -1;
#endif
}

Debugger::~Debugger()
{
  while (history) {
    struct _history *n = history->next;
    free(history->msg);
    delete(history);
    history = n;
  }
  history = NULL;
}

bool getAddress(const char *buf, unsigned int *addrOut)
{
  unsigned int val;
  if (sscanf(buf, " 0x%X", &val) == 1 ||
      sscanf(buf, " 0x%x", &val) == 1
      ) {
    *addrOut = val;
    return true;
  } else if (sscanf(buf, " $%X", &val) == 1 ||
	     sscanf(buf, " $%x", &val) == 1
	     ) {
    *addrOut = val;
    return true;
  } else if (sscanf(buf, " %d", &val) == 1) {
    *addrOut = val;
    return true;
  }
  return false;
}

bool getTwoAddresses(const char *buf, unsigned int *addrOut1, unsigned int *addrOut2)
{
  unsigned int val, val2;
  if (sscanf(buf, " 0x%X 0x%X", &val, &val2) == 2 ||
      sscanf(buf, " 0x%x 0x%X", &val, &val2) == 2
      ) {
    *addrOut1 = val;
    *addrOut2 = val2;
    return true;
  } else if (sscanf(buf, " $%X $%X", &val, &val2) == 2 ||
	     sscanf(buf, " $%x $%X", &val, &val2) == 2
	     ) {
    *addrOut1 = val;
    *addrOut2 = val2;
    return true;
  } else if (sscanf(buf, " %d %d", &val, &val2) == 2) {
    *addrOut1 = val;
    *addrOut2 = val2;
    return true;
  }
  return false;
}

#define GETCH { if ((read(cd,&b,1)) == -1) { close(cd); cd=-1; return; } }

#define GETLN {   int ptr=0;   while (((read(cd,&b,1)) != -1) && ptr < sizeof(buf) && b != 10 && b != 13) {   if (b) {buf[ptr++] = b;}   }   buf[ptr]=0; }

#define HEXCHAR(x) ((x>='0'&&x<='9')?x-'0':(x>='a'&&x<='f')?x-'a'+10:(x>='A'&&x<='F')?x-'A'+10:(x=='i' || x=='I')?1:(x=='o' || x=='O')?0:0)
#define FROMHEXP(p) ((HEXCHAR(*p) << 4) | HEXCHAR(*(p+1)))

// Fold an Apple screen code (normal / inverse / flashing / lowercase) down to a
// printable ASCII byte for a text dump.
static char foldScreenChar(uint8_t c)
{
  c &= 0x7F;
  if (c < 0x20) c += 0x40;
  return (char)c;
}

// The first byte of visible text row 'row' (0..23). Apple II text rows are
// interleaved: base + 0x80*(row&7) + 0x28*(row>>3). See deinterlaceAddress().
static uint16_t textRowBase(uint16_t base, uint8_t row)
{
  return base + 0x80 * (row & 7) + 0x28 * (row >> 3);
}

// Dump 24 rows of 40-column text read straight from main RAM (variant 0),
// bypassing the memory soft switches - matching redraw40ColumnText().
static void dumpText40(int cd, MMU *mmu, uint16_t base)
{
  char buf[64];
  snprintf(buf, sizeof(buf), "40-column text at $%04X:\r\n", base);
  if (write(cd, buf, strlen(buf)) != (ssize_t)strlen(buf)) return;
  for (uint8_t row = 0; row < 24; row++) {
    uint16_t rowBase = textRowBase(base, row);
    char line[42];
    for (uint8_t col = 0; col < 40; col++) {
      line[col] = foldScreenChar(mmu->readDirect(rowBase + col, 0));
    }
    line[40] = '\r'; line[41] = '\n';
    if (write(cd, line, 42) != 42) return;
  }
}

// Dump 24 rows of 80-column text. 80-column text always lives on page 1
// ($400); aux RAM (variant 1) holds the even (left) column of each cell and
// main RAM (variant 0) the odd (right) column - matching redraw80ColumnText().
static void dumpText80(int cd, MMU *mmu)
{
  const char *hdr = "80-column text at $0400 (aux=even cols, main=odd cols):\r\n";
  if (write(cd, hdr, strlen(hdr)) != (ssize_t)strlen(hdr)) return;
  for (uint8_t row = 0; row < 24; row++) {
    uint16_t rowBase = textRowBase(0x400, row);
    char line[82];
    for (uint8_t i = 0; i < 40; i++) {
      line[i*2]     = foldScreenChar(mmu->readDirect(rowBase + i, 1)); // aux, even col
      line[i*2 + 1] = foldScreenChar(mmu->readDirect(rowBase + i, 0)); // main, odd col
    }
    line[80] = '\r'; line[81] = '\n';
    if (write(cd, line, 82) != 82) return;
  }
}

void Debugger::step()
{
  static char buf[256];
  uint8_t cmdbuf[50];

  // FIXME: add more than just RTS(0x60) here
  if (steppingOut &&
      g_vm->getMMU()->read(g_cpu->pc) != 0x60) {
    return;
  }
  steppingOut = false;
  
    addCurrentPCToHistory();
    
    if (!singleStep && !isBreakpointAt(g_cpu->pc)) {
      // Running until we reach any breakpoint
      return;
    }
    singleStep = false; // we have taken a single step, so reset flag

    uint8_t b; // byte value used in parsing
    unsigned int val; // common value buffer used in parsing

    if (cd != -1) {
      // Print the status back out the socket
      uint8_t p = g_cpu->flags;
      snprintf(buf, sizeof(buf), "OP: $%02x A: %02x  X: %02x  Y: %02x  PC: $%04x  SP: %02x  Flags: %c%cx%c%c%c%c%c\n",
	       g_vm->getMMU()->read(g_cpu->pc),
	       g_cpu->a, g_cpu->x, g_cpu->y, g_cpu->pc, g_cpu->sp,
	       p & (1<<7) ? 'N':' ',
	       p & (1<<6) ? 'V':' ',
	       p & (1<<4) ? 'B':' ',
	       p & (1<<3) ? 'D':' ',
	       p & (1<<2) ? 'I':' ',
	       p & (1<<1) ? 'Z':' ',
	       p & (1<<0) ? 'C':' '
	       );
      if (write(cd, buf, strlen(buf)) != strlen(buf)) {
	close(cd);
	cd=-1;
	return;
      }
    }

  doover:
    // Show a prompt
    snprintf(buf, sizeof(buf), "debug [$%X]> ", g_cpu->pc);
    if (write(cd, buf, strlen(buf)) != strlen(buf)) {
      close(cd);
      cd=-1;
      return;
    }
    do {
      GETCH;
    } while (b != 'c' && // continue (with any breakpoint set)
	     b != 'q' && // quit
	     b != 's' && // single step
	     b != 'S' && // step out
	     b != 'b' && // set breakpoint
	     b != 'd' && // show disassembly
	     b != 'L' && // load memory (lines)
             b != 'D' && // dump memory
	     b != 'h' && // show history
	     b != 'T' && // dump text screen
	     b != 'K' && // inject keyboard input
	     b != 'y' && // show cycle count
	     b != 'G' && // goto (set PC)
	     b != '*'    // show memory (byte)
	     );

    switch (b) {
    case 'c': // continue (if there is any breakpoint set)
      if (isAnyBreakpointSet()) {
	snprintf(buf, sizeof(buf), "Continuing until any breakpoint\012\015");
	write(cd, buf, strlen(buf));
      } else {
	snprintf(buf, sizeof(buf), "No breakpoint to continue until\012\015");
	write(cd, buf, strlen(buf));
	goto doover;
      }
      break;
      
    case 'h': // show history
      {
	struct _history *h = history;
	uint32_t i = 0;
	while (h) {
	  snprintf(buf, sizeof(buf), "%d ", i++);
	  write(cd, buf, strlen(buf));
	  write(cd, h->msg, strlen(h->msg));
	  h = h->next;
	}
      }
      goto doover;
      
    case 'q': // Close debugging socket and quit
      printf("Closing debugging socket\n");
      removeAllBreakpoints();
      close(cd); cd=-1;
      break;
      
    case 's':
      singleStep = true; // for when any breakpoint is set: just step once
      for (int idx=0; idx<sizeof(cmdbuf); idx++) {
	cmdbuf[idx] = g_vm->getMMU()->read(g_cpu->pc+idx);
      }
      dis.instructionToMnemonic(g_cpu->pc, cmdbuf, buf, sizeof(buf));
      write(cd, buf, strlen(buf));
      buf[0] = 13;
      buf[1] = 10;
      write(cd, buf, 2);
      
      break;
      
    case 'S':
      steppingOut = true;
      break;
      
    case 'b': // Set or remove all breakpoints
      GETLN;
      if (getAddress(buf, &val)) {
	if (addBreakpoint(val)) {
	  snprintf(buf, sizeof(buf), "Breakpoint set for 0x%X\012\015", val);
	} else {
	  snprintf(buf, sizeof(buf), "Failed to set breakpoint for 0x%X\012\015", val);
	}
      } else {
	removeAllBreakpoints();
	snprintf(buf, sizeof(buf), "All breakpoints removed\012\015");
      }
      write(cd, buf, strlen(buf));
      break;
      
    case 'd': // show disassembly @ PC
      { 
	uint16_t loc=g_cpu->pc;
	for (int i=0; i<50/3; i++) {
	  for (int idx=0; idx<sizeof(cmdbuf); idx++) {
	    cmdbuf[idx] = g_vm->getMMU()->read(loc+idx);
	  }
	  loc += dis.instructionToMnemonic(loc, cmdbuf, buf, sizeof(buf));
	  write(cd, buf, strlen(buf));
	  buf[0] = 13;
	  buf[1] = 10;
	  write(cd, buf, 2);
	}
      }
      goto doover;
      
    case 'L': // Load data to memory. Use: "L 0x<address>\n" followed by lines of packed hex; ends with a blank line
      {
	printf("Loading data\n");
	GETLN;
	if (getAddress(buf, &val)) {
	  printf("Load data address: 0x%X\n", val);
	  uint16_t address = val;
	  while (1) {
	    GETLN;
	    if (strlen(buf)==0)
	      break;
	    const char *p = buf;
	    while (*p && *(p+1)) {
	      val = FROMHEXP(p);
	      printf("0x%.2X ", val);
	      g_vm->getMMU()->write(address++, val);
	      p+=2;
	    }
	    printf("\n");
	  }
	}
      }
      goto doover;

    case 'D': // Dump memory. Use "D 0x<address> 0x<length>\n"
      {
        unsigned int val2;
        GETLN;
        if (getTwoAddresses(buf, &val, &val2)) {
          snprintf(buf, sizeof(buf), "Memory dump at 0x%X, length 0x%X:\r\n", val, val2);
          write(cd, buf, strlen(buf));
          for (uint32_t i=val; i<val+val2; i+=16) {
            snprintf(buf, sizeof(buf), "$%.4X  ", i);
            write(cd, buf, strlen(buf));
            for (uint8_t j=0; j<16 && (i+j)<(val+val2); j++) {
              uint8_t v = g_vm->getMMU()->read(i+j);
              snprintf(buf, sizeof(buf), "%.2X ", v);
            write(cd, buf, strlen(buf));
            }
            snprintf(buf, sizeof(buf), "\r\n");
            write(cd, buf, strlen(buf));
          }
        } else {
          snprintf(buf, sizeof(buf), "Syntax error\12\15");
          write(cd, buf, strlen(buf));
        }
      }
      goto doover;
      
    case 'T': // Dump the text screen as ASCII, in display order. With no
              // argument, auto-detect 40- vs 80-column mode and the active
              // page from the video soft switches and dump what's on screen.
              // With an explicit base ("T 0x800") force a 40-column dump.
      {
        GETLN;
        MMU *mmu = g_vm->getMMU();
        if (getAddress(buf, &val)) {
          dumpText40(cd, mmu, val);
          goto doover;
        }
        // Read the video status soft switches (all side-effect-free reads).
        bool col80   = (mmu->read(0xC01F) & 0x80); // RD80VID   (S_80COL)
        bool page2   = (mmu->read(0xC01C) & 0x80); // RDPAGE2   (S_PAGE2)
        bool store80 = (mmu->read(0xC018) & 0x80); // RD80STORE (S_80STORE)
        bool textOn  = (mmu->read(0xC01A) & 0x80); // RDTEXT    (S_TEXT)
        bool mixed   = (mmu->read(0xC01B) & 0x80); // RDMIXED   (S_MIXED)

        snprintf(buf, sizeof(buf), "mode: %s, %s%s%s\r\n",
                 col80 ? "80-column" : "40-column",
                 textOn ? "TEXT" :
                   (mixed ? "MIXED (only bottom 4 rows shown over graphics)" :
                            "GRAPHICS (text below is in RAM but not on screen)"),
                 store80 ? ", 80STORE" : "",
                 page2 ? ", PAGE2" : "");
        write(cd, buf, strlen(buf));

        if (col80) {
          dumpText80(cd, mmu);
        } else {
          dumpText40(cd, mmu, page2 ? 0x800 : 0x400);
        }
      }
      goto doover;

    case 'K': // Inject keyboard input. "K <text>" queues keystrokes that the
              // emulator types as it runs; they are delivered one at a time as
              // the running program reads each keyboard strobe, so nothing is
              // dropped. Continue ('c'/breakpoint) or quit ('q') to let the CPU
              // run so the keys are consumed. Backslash escapes: \r or \n =
              // Return, \t = Tab, \e = Esc, \0 = NUL, \\ = backslash, \xHH = a
              // raw hex byte.
      {
        GETLN;
        const char *p = buf;
        if (*p == ' ') p++; // skip one separating space after the 'K'
        AppleKeyboard *kbd = (AppleKeyboard *)g_vm->getKeyboard();
        unsigned int queued = 0;
        bool full = false;
        while (*p) {
          uint8_t c;
          if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
            case 'r': case 'n': c = 0x0D; break;
            case 't':           c = 0x09; break;
            case 'e':           c = 0x1B; break;
            case '0':           c = 0x00; break;
            case '\\':          c = 0x5C; break;
            case 'x':
              if (*(p+1) && *(p+2)) { c = FROMHEXP((p+1)); p += 2; }
              else                  { c = 'x'; }
              break;
            default:            c = (uint8_t)*p; break;
            }
            p++;
          } else {
            c = (uint8_t)*p++;
          }
          if (!kbd->injectByte(c)) { full = true; break; }
          queued++;
        }
        snprintf(buf, sizeof(buf), "Queued %u key%s%s (queue depth now %u)\r\n",
                 queued, queued == 1 ? "" : "s",
                 full ? " (queue full, remainder dropped)" : "",
                 (unsigned int)kbd->injectQueueDepth());
        write(cd, buf, strlen(buf));
      }
      goto doover;

    case '*': // read 1 byte of memory. Use '* 0x<address>'
      {
	GETLN;
	if (getAddress(buf, &val)) {
	  snprintf(buf, sizeof(buf), "Memory location 0x%X: ", val);
	  write(cd, buf, strlen(buf));
	  val = g_vm->getMMU()->read(val);
	  snprintf(buf, sizeof(buf), "0x%.2X\012\015", val);
	  write(cd, buf, strlen(buf));
	} else {
	  snprintf(buf, sizeof(buf), "Invalid read\012\015");
	  write(cd, buf, strlen(buf));
	}
      }
      goto doover;
      
    case 'y': // show cumulative cycle count and 1x-equivalent time
      snprintf(buf, sizeof(buf), "cycles=%lld  t1x=%.3fs\012\015",
	       (long long)g_cpu->cycles,
	       (double)g_cpu->cycles / 1023000.0);
      write(cd, buf, strlen(buf));
      goto doover;

    case 'G': // Goto (set PC)
      GETLN;
      if (getAddress(buf, &val)) {
	snprintf(buf, sizeof(buf), "Setting PC to 0x%X\012\015", val);
	write(cd, buf, strlen(buf));
	g_cpu->pc = val;
	printf("Closing debugging socket\n");
	close(cd); cd=-1;
      } else {
	snprintf(buf, sizeof(buf), "sscanf failed, skipping\012\015");
	write(cd, buf, strlen(buf));
      }
      break;
      
      // ... ?
      //   b - set breakpoint
      //   s - step over
      //   S - step out
      //   c - continue (close connection)
      //   d - disassemble @ current PC
      //   L - load data to memory
      //   G - Goto (set PC)
    }
}



void Debugger::setSocket(int fd)
{
  printf("New debugger session established\n");
  cd = fd;
  singleStep = true; // want to stop
}

bool Debugger::active()
{
  return (cd != -1);
}


bool Debugger::addBreakpoint(uint16_t addr)
{
  for (int i=0; i<MAX_BREAKPOINTS; i++) {
    if (breakpoints[i] == 0) {
      breakpoints[i] = addr;
      return true;
    }
  }
  return false;
}

bool Debugger::isAnyBreakpointSet()
{
  for (int i=0; i<MAX_BREAKPOINTS; i++) {
    if (breakpoints[i]) return true;
  }
  return false;
}

bool Debugger::isBreakpointAt(uint16_t addr)
{
  for (int i=0; i<MAX_BREAKPOINTS; i++) {
    if (breakpoints[i] == addr) return true;
  }
  return false;
}

bool Debugger::removeBreakpoint(uint16_t addr)
{
  for (int i=0; i<MAX_BREAKPOINTS; i++) {
    if (breakpoints[i] == addr) {
      breakpoints[i] = 0;
      return true;
    }
  }
  return false;
}

void Debugger::removeAllBreakpoints()
{
  for (int i=0; i<MAX_BREAKPOINTS; i++) {
    breakpoints[i] = 0;
  }
}

void Debugger::addStringToHistory(const char *s)
{
  struct _history *_newp = new struct _history;
  _newp->msg = strdup(s);
  _newp->next = NULL;

  if (endh) endh->next = _newp;
  endh = _newp;

  if (!history) history = _newp;
  historyCount++;

  if (historyCount > MAX_HISTORY) {
    struct _history *freeme = history;
    history = history->next;
    free(freeme->msg);
    delete freeme;
  }
}

void Debugger::addCurrentPCToHistory()
{
  // Get it as a disassembled hunk; add the flags; and then put it in
  // the history
  uint8_t toDisassemble[3];
  char buf[255];
  toDisassemble[0] = g_vm->getMMU()->read(g_cpu->pc);
  toDisassemble[1] = g_vm->getMMU()->read(g_cpu->pc+1);
  toDisassemble[2] = g_vm->getMMU()->read(g_cpu->pc+2);
  dis.instructionToMnemonic(g_cpu->pc, toDisassemble, buf, sizeof(buf));

  uint8_t p = g_cpu->flags;

  while (strlen(buf) < 35) {
    strcat(buf, " ");
  }
  snprintf(&buf[strlen(buf)], sizeof(buf) - strlen(buf), " ;; OP: $%02x A: %02x  X: %02x  Y: %02x  PC: $%04x SP: %02x S: %.2x Flags: %c%cx%c%c%c%c%c\012\015",
           g_vm->getMMU()->read(g_cpu->pc),
           g_cpu->a, g_cpu->x, g_cpu->y, g_cpu->pc, g_cpu->sp,
	  p,
           p & (1<<7) ? 'N':' ',
           p & (1<<6) ? 'V':' ',
           p & (1<<4) ? 'B':' ',
           p & (1<<3) ? 'D':' ',
           p & (1<<2) ? 'I':' ',
           p & (1<<1) ? 'Z':' ',
           p & (1<<0) ? 'C':' '
           );
  addStringToHistory(buf);
}
