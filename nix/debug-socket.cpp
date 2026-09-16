#include "debug-socket.h"
#include "debugger.h"
#include "disassembler.h"
#include "globals.h"
#include "cpu.h"
#include "applemmu.h"
#include "physicalkeyboard.h"
#include "sdl-keyboard.h"
#include "sdl-display.h"
#include "sdl-mouse.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>

DebugSocket g_debugSocket;

static Disassembler dis;
static const DebugBank cpuBank = { DBG_BANK_CPU, 0, 1 };

static void *listener_thread(void *objptr)
{
  ((DebugSocket *)objptr)->serve();
  return NULL;
}

DebugSocket::DebugSocket()
{
  sd = -1;
  cd = -1;
  head = tail = NULL;
  newClient = false;
  clientClosed = false;
  pthread_mutex_init(&lock, NULL);
}

void DebugSocket::listenOn(uint16_t port)
{
  if (sd != -1)
    return; // already listening
  if (port == 0) {
    printf("Debug socket disabled\n");
    fflush(stdout);
    return;
  }

  struct sockaddr_in server;
  int optval;

  sd = socket(AF_INET, SOCK_STREAM, 0);

  optval=1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR,
	     (void*)&optval, sizeof(optval));

  memset(&server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(port);

  if (bind(sd, (struct sockaddr *) &server, sizeof(server)) < 0) {
    // Almost always a second instance on the same port. Say which port,
    // and how to move: dying with a bare "Address already in use" makes
    // this look like a bug rather than a collision.
    fprintf(stderr, "error binding debug socket to port %u: %s\n",
	    (unsigned)port, strerror(errno));
    fprintf(stderr, "  (another aiie is probably using it; "
	    "pass -p <port> to move, or -p 0 to disable)\n");
    exit(1);
  }

  listen(sd,5);

  printf("Debug socket listening on port %u\n", (unsigned)port);
  fflush(stdout);

  pthread_create(&thread, NULL, &listener_thread, (void *)this);
}

// ---- the listener thread --------------------------------------------------

// One byte from the client, or -1 when it has gone away.
static int getch(int fd)
{
  uint8_t b;
  ssize_t n = read(fd, &b, 1);
  if (n <= 0) return -1;
  return b;
}

// The rest of a line, without its terminator. Returns false when the client
// has gone away. Either \n or \r ends a line, as the old parser had it, so
// a client that sends \r\n gets an empty line after every command; those
// are harmless everywhere except inside an L block.
static bool getline_(int fd, char *buf, size_t n)
{
  size_t ptr = 0;
  while (1) {
    int b = getch(fd);
    if (b < 0) return false;
    if (b == 10 || b == 13) break;
    if (b && ptr < n - 1) buf[ptr++] = (char)b;
  }
  buf[ptr] = 0;
  return true;
}

void DebugSocket::serve()
{
  while (1) {
    struct sockaddr_in client;
    socklen_t clilen = sizeof(client);
    int fd = accept(sd, (struct sockaddr *)&client, &clilen);
    if (fd < 0) {
      perror("ERROR on accept");
      exit(1);
    }

    printf("New debugger session established\n");
    fflush(stdout);
    cd = fd;
    newClient = true;

    // Parse commands off the wire and queue each complete one. Single
    // letter commands go as soon as the letter arrives (an interactive
    // client in raw mode sends no newline after them); commands with an
    // argument take the rest of their line; L takes its address line and
    // every data line up to a blank one.
    char line[1024];
    int pending = -1;   // a byte read ahead by 'd' that belongs to the next command
    while (1) {
      int b = pending >= 0 ? pending : getch(fd);
      pending = -1;
      if (b < 0) break;
      switch (b) {
      case 'c': case 's': case 'S': case 'h': case 'y':
      case 'q': case 'p':
	{
	  char one[2] = { (char)b, 0 };
	  push(one, 1);
	}
	break;

      case 'd':
	{
	  // "d" alone disassembles at PC and goes at once, so a raw-mode
	  // client need not press Return; "d <addr>" takes the rest of
	  // its line. One byte of lookahead tells them apart.
	  int n = getch(fd);
	  if (n < 0) goto gone;
	  if (n == ' ') {
	    if (!getline_(fd, line, sizeof(line))) goto gone;
	    size_t len = strlen(line);
	    char *cmd = (char *)malloc(len + 3);
	    cmd[0] = 'd'; cmd[1] = ' ';
	    memcpy(cmd + 2, line, len + 1);
	    push(cmd, len + 2);
	    free(cmd);
	  } else {
	    push("d", 1);
	    pending = n;
	  }
	}
	break;

      case 'b': case 'D': case 'T': case 'K': case '*': case 'G':
      case 'w': case 'W': case 'B': case 'P': case 'M':
	{
	  if (!getline_(fd, line, sizeof(line))) goto gone;
	  size_t len = strlen(line);
	  char *cmd = (char *)malloc(len + 2);
	  cmd[0] = (char)b;
	  memcpy(cmd + 1, line, len + 1);
	  push(cmd, len + 1);
	  free(cmd);
	}
	break;

      case 'Q':
	{
	  // "Q y" quits on one line. A bare "Q" gets the question and one
	  // more line for the answer; asked here, since the command does
	  // not reach the emulator until the answer is in.
	  if (!getline_(fd, line, sizeof(line))) goto gone;
	  const char *p = line;
	  while (*p == ' ' || *p == '\t') p++;
	  char cmd[1040];
	  if (*p) {
	    snprintf(cmd, sizeof(cmd), "Q %s", p);
	  } else {
	    const char *ask = "Really quit the emulator? Unsaved disk changes will be"
	      " lost. [y/N] ";
	    if (write(fd, ask, strlen(ask)) < 0) goto gone;
	    if (!getline_(fd, line, sizeof(line))) goto gone;
	    snprintf(cmd, sizeof(cmd), "Q %s", line);
	  }
	  push(cmd, strlen(cmd));
	}
	break;

      case 'L':
	{
	  // "L <addr>\n" then lines of packed hex, ending with a blank line.
	  // Queued as one command with the lines joined by newlines.
	  if (!getline_(fd, line, sizeof(line))) goto gone;
	  size_t cap = 4096, len = 0;
	  char *cmd = (char *)malloc(cap);
	  cmd[len++] = 'L';
	  size_t l = strlen(line);
	  memcpy(cmd + len, line, l); len += l;
	  while (1) {
	    if (!getline_(fd, line, sizeof(line))) { free(cmd); goto gone; }
	    l = strlen(line);
	    if (l == 0) break;
	    if (len + l + 2 > cap) {
	      cap = (cap + l + 2) * 2;
	      cmd = (char *)realloc(cmd, cap);
	    }
	    cmd[len++] = '\n';
	    memcpy(cmd + len, line, l); len += l;
	  }
	  cmd[len] = 0;
	  push(cmd, len);
	  free(cmd);
	}
	break;

      default:
	// Whitespace, the newline after a single-letter command, or a
	// letter that is not a command: skip it.
	break;
      }
    }

  gone:
    // The emulator thread owns the descriptor from here: it writes the
    // last replies, closes it, and clears cd. Wait for that before
    // accepting the next client, so the two never share a descriptor.
    clientClosed = true;
    while (cd != -1) usleep(10000);
    clientClosed = false;
  }
}

void DebugSocket::push(const char *text, size_t len)
{
  Cmd *c = new Cmd;
  c->text = (char *)malloc(len + 1);
  memcpy(c->text, text, len);
  c->text[len] = 0;
  c->next = NULL;
  pthread_mutex_lock(&lock);
  if (tail) tail->next = c; else head = c;
  tail = c;
  pthread_mutex_unlock(&lock);
}

DebugSocket::Cmd *DebugSocket::take()
{
  pthread_mutex_lock(&lock);
  Cmd *c = head;
  if (c) {
    head = c->next;
    if (!head) tail = NULL;
  }
  pthread_mutex_unlock(&lock);
  return c;
}

// ---- the emulator thread --------------------------------------------------

void DebugSocket::reply(const char *s)
{
  if (cd == -1) return;
  size_t len = strlen(s);
  if (write(cd, s, len) != (ssize_t)len) {
    clientClosed = true;
  }
}

void DebugSocket::replyf(const char *fmt, ...)
{
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  reply(buf);
}

void DebugSocket::prompt()
{
  replyf("debug [$%X]> ", g_cpu->pc);
}

void DebugSocket::statusLine()
{
  uint8_t p = g_cpu->flags;
  replyf("OP: $%02x A: %02x  X: %02x  Y: %02x  PC: $%04x  SP: %02x  Flags: %c%cx%c%c%c%c%c\n",
	 g_debugger.peek(g_cpu->pc, cpuBank),
	 g_cpu->a, g_cpu->x, g_cpu->y, g_cpu->pc, g_cpu->sp,
	 p & (1<<7) ? 'N':' ',
	 p & (1<<6) ? 'V':' ',
	 p & (1<<4) ? 'B':' ',
	 p & (1<<3) ? 'D':' ',
	 p & (1<<2) ? 'I':' ',
	 p & (1<<1) ? 'Z':' ',
	 p & (1<<0) ? 'C':' ');
}

void DebugSocket::reportHalt()
{
  DebugHalt h;
  if (!g_debugger.takeHalt(&h)) return;
  switch (h.reason) {
  case DBG_HALT_BREAKPOINT:
    replyf("Breakpoint at $%04X\r\n", h.addr);
    break;
  case DBG_HALT_WATCHPOINT:
    replyf("Watchpoint: %s $%04X = $%02X\r\n", h.isWrite ? "write" : "read", h.addr, h.value);
    break;
  case DBG_HALT_RUNTO:
    replyf("Reached $%04X\r\n", h.addr);
    break;
  default:
    break;
  }
  statusLine();
  prompt();
}

void DebugSocket::poll()
{
  if (cd == -1) return;

  if (newClient) {
    newClient = false;
    // Connecting no longer pauses the machine; 'p' or a breakpoint does.
    if (g_debugger.state() == DBG_PAUSED) statusLine();
    prompt();
  }

  Cmd *c;
  while ((c = take()) != NULL) {
    handle(c->text);
    free(c->text);
    delete c;
  }

  reportHalt();

  if (clientClosed) {
    printf("Closing debugging socket\n");
    fflush(stdout);
    // shutdown() wakes the listener's blocking read; a bare close() does
    // not reliably do that from another thread.
    shutdown(cd, SHUT_RDWR);
    close(cd);
    cd = -1;   // lets the listener accept the next client
    // A client that went away does not get to leave the machine frozen.
    if (g_debugger.state() == DBG_PAUSED) g_debugger.cont();
  }
}

// ---- command parsing ------------------------------------------------------

static bool getAddress(const char *buf, unsigned int *addrOut)
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

static bool getTwoAddresses(const char *buf, unsigned int *addrOut1, unsigned int *addrOut2)
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

#define HEXCHAR(x) ((x>='0'&&x<='9')?x-'0':(x>='a'&&x<='f')?x-'a'+10:(x>='A'&&x<='F')?x-'A'+10:(x=='i' || x=='I')?1:(x=='o' || x=='O')?0:0)
#define FROMHEXP(p) ((HEXCHAR(*p) << 4) | HEXCHAR(*(p+1)))

// One disassembled line in the listing format ("$ADDR  bytes  MN  arg").
static uint8_t disassembleAt(uint16_t addr, char *buf, size_t n)
{
  uint8_t bytes[3];
  g_debugger.peekRange(addr, bytes, 3, cpuBank);
  return dis.instructionToMnemonic(addr, bytes, buf, (uint16_t)n);
}

void DebugSocket::handle(char *cmd)
{
  char buf[256];
  unsigned int val, val2;
  const char *args = cmd + 1;

  switch (cmd[0]) {
  case 'c': // continue
    if (g_debugger.breakpointCount() || g_debugger.watchpointCount()) {
      reply("Continuing until any breakpoint\012\015");
    } else {
      reply("Continuing\012\015");
    }
    g_debugger.cont();
    break;

  case 'p': // pause; the halt report follows
    g_debugger.pause();
    break;

  case 's': // step one instruction; shows what is about to run
    disassembleAt(g_cpu->pc, buf, sizeof(buf));
    reply(buf);
    reply("\r\n");
    g_debugger.stepIn();
    break;

  case 'S': // step out
    g_debugger.stepOut();
    break;

  case 'b': // set a breakpoint, or clear them all
    if (getAddress(args, &val)) {
      if (g_debugger.addBreakpoint((uint16_t)val) >= 0) {
	replyf("Breakpoint set for 0x%X\012\015", val);
      } else {
	replyf("Failed to set breakpoint for 0x%X\012\015", val);
      }
    } else {
      g_debugger.clearBreakpoints();
      reply("All breakpoints removed\012\015");
    }
    prompt();
    break;

  case 'w': // watchpoint: "w <from> [to] [r|w|rw]"; "w" alone lists
    {
      // Tokenize on spaces.
      char *tok[4] = { NULL, NULL, NULL, NULL };
      int ntok = 0;
      char *p = cmd + 1;
      while (*p && ntok < 4) {
	while (*p == ' ' || *p == '\t') p++;
	if (!*p) break;
	tok[ntok++] = p;
	while (*p && *p != ' ' && *p != '\t') p++;
	if (*p) *p++ = 0;
      }
      if (ntok == 0) {
	int n = g_debugger.watchpointCount();
	if (!n) reply("No watchpoints\r\n");
	for (int i = 0; i < n; i++) {
	  const DebugWatchpoint *w = g_debugger.watchpoint(i);
	  replyf("%d: $%04X-$%04X %s%s%s\r\n", i, w->from, w->to,
		 w->onRead ? "r" : "", w->onWrite ? "w" : "",
		 w->enabled ? "" : " (disabled)");
	}
	prompt();
	break;
      }
      unsigned int from, to;
      bool onRead = false, onWrite = false;
      const char *mode = NULL;
      if (!getAddress(tok[0], &from)) {
	reply("Syntax: w <from> [to] [r|w|rw]\r\n");
	prompt();
	break;
      }
      to = from;
      if (ntok >= 2) {
	if (getAddress(tok[1], &to)) {
	  if (ntok >= 3) mode = tok[2];
	} else {
	  to = from;
	  mode = tok[1];
	}
      }
      if (!mode) mode = "rw";
      for (const char *m = mode; *m; m++) {
	if (*m == 'r' || *m == 'R') onRead = true;
	if (*m == 'w' || *m == 'W') onWrite = true;
      }
      int idx = g_debugger.addWatchpoint((uint16_t)from, (uint16_t)to, onRead, onWrite);
      if (idx >= 0) {
	replyf("Watchpoint %d set for $%04X-$%04X (%s%s)\r\n", idx, from, to,
	       onRead ? "r" : "", onWrite ? "w" : "");
      } else {
	reply("Failed to set watchpoint (table full)\r\n");
      }
      prompt();
    }
    break;

  case 'W': // clear all watchpoints
    g_debugger.clearWatchpoints();
    reply("All watchpoints removed\r\n");
    prompt();
    break;

  case 'h': // trace listing; turns tracing on the first time
    if (!g_debugger.tracing()) {
      g_debugger.setTrace(true);
      reply("Trace on; instruction history accumulates from here\r\n");
    } else {
      uint32_t n = g_debugger.traceCount();
      for (uint32_t i = 0; i < n; i++) {
	uint16_t pc = g_debugger.traceEntry(n - 1 - i);   // oldest first
	disassembleAt(pc, buf, sizeof(buf));
	replyf("%u %s\r\n", (unsigned)i, buf);
      }
    }
    prompt();
    break;

  case 'q': // detach; the machine runs on
    if (g_debugger.state() == DBG_PAUSED) g_debugger.cont();
    clientClosed = true;
    break;

  case 'Q': // quit the emulator, confirmed on the socket (see the listener)
    {
      // Confirmation happens on the socket rather than in the window's
      // native modal, because nobody is at the keyboard to answer that.
      // A confirmed quit is exit(0), exactly as the GUI's is, which means
      // atexit() runs and PREFERENCES ARE WRITTEN: a scratch instance
      // quitting this way saves its own disks and window size.
      const char *p = args;
      while (*p == ' ' || *p == '\t') p++;
      if (*p != 'y' && *p != 'Y') {
	reply("Not quitting\012\015");
	prompt();
	break;
      }
      reply("Quitting\012\015");
      printf("Quit requested over the debug socket\n");
      fflush(stdout);
      close(cd); cd = -1;
      exit(0);
    }

  case 'd': // disassembly from PC, or from an address
    {
      uint16_t loc = g_cpu->pc;
      if (getAddress(args, &val)) loc = (uint16_t)val;
      for (int i = 0; i < 50/3; i++) {
	loc += disassembleAt(loc, buf, sizeof(buf));
	reply(buf);
	reply("\r\n");
      }
      prompt();
    }
    break;

  case 'L': // load memory: "L <addr>" then hex lines, joined by newlines
    {
      char *nl = strchr(cmd, '\n');
      if (nl) *nl = 0;
      if (getAddress(args, &val)) {
	printf("Load data address: 0x%X\n", val);
	uint16_t address = (uint16_t)val;
	unsigned int count = 0;
	char *p = nl ? nl + 1 : NULL;
	while (p && *p) {
	  char *end = strchr(p, '\n');
	  if (end) *end = 0;
	  while (*p) {
	    if (*p == ' ' || *p == '\t') { p++; continue; }   // "A9 41" and "A941" both load
	    if (!*(p+1)) break;
	    uint8_t v = FROMHEXP(p);
	    g_debugger.poke(address++, v, cpuBank);
	    count++;
	    p += 2;
	  }
	  p = end ? end + 1 : NULL;
	}
	printf("Loaded %u bytes\n", count);
      }
      prompt();
    }
    break;

  case 'D': // dump memory: "D <addr> <len>", what the CPU sees; "D main
            // <addr> <len>" and "D aux <addr> <len>" read one bank regardless
            // of the switches (the stock aux bank, language card bank 1)
    {
      static const DebugBank mainBank = { DBG_BANK_MAIN, 0, 1 };
      static const DebugBank auxBank  = { DBG_BANK_AUX,  0, 1 };
      const DebugBank *bank = &cpuBank;
      const char *p = args;
      while (*p == ' ') p++;
      if (!strncmp(p, "aux", 3))       { bank = &auxBank;  p += 3; }
      else if (!strncmp(p, "main", 4)) { bank = &mainBank; p += 4; }
      if (getTwoAddresses(p, &val, &val2)) {
	replyf("Memory dump at 0x%X, length 0x%X%s:\r\n", val, val2,
	       bank == &auxBank ? " (aux)" : bank == &mainBank ? " (main)" : "");
	for (uint32_t i = val; i < val + val2; i += 16) {
	  replyf("$%.4X  ", i);
	  for (uint8_t j = 0; j < 16 && (i+j) < (val+val2); j++) {
	    replyf("%.2X ", g_debugger.peek((uint16_t)(i+j), *bank));
	  }
	  reply("\r\n");
	}
      } else {
	reply("Syntax error\12\15");
      }
    }
    prompt();
    break;

  case 'T': // text screen dump. "T" detects the mode; "T <base>" forces 40 columns
    {
      char screen[24][80];
      if (getAddress(args, &val)) {
	g_debugger.textScreen(screen, (uint16_t)val);
	replyf("40-column text at $%04X:\r\n", val);
	for (int row = 0; row < 24; row++) {
	  memcpy(buf, screen[row], 40); buf[40] = '\r'; buf[41] = '\n'; buf[42] = 0;
	  reply(buf);
	}
	prompt();
	break;
      }
      uint16_t sw = g_debugger.softSwitches();
      bool col80   = sw & S_80COL;
      bool page2   = sw & S_PAGE2;
      bool store80 = sw & S_80STORE;
      bool textOn  = sw & S_TEXT;
      bool mixed   = sw & S_MIXED;
      replyf("mode: %s, %s%s%s\r\n",
	     col80 ? "80-column" : "40-column",
	     textOn ? "TEXT" :
	       (mixed ? "MIXED (only bottom 4 rows shown over graphics)" :
			"GRAPHICS (text below is in RAM but not on screen)"),
	     store80 ? ", 80STORE" : "",
	     page2 ? ", PAGE2" : "");
      uint8_t cols = g_debugger.textScreen(screen, 0);
      if (cols == 80) {
	reply("80-column text at $0400 (aux=even cols, main=odd cols):\r\n");
      } else {
	replyf("40-column text at $%04X:\r\n", (page2 && !store80) ? 0x800 : 0x400);
      }
      for (int row = 0; row < 24; row++) {
	memcpy(buf, screen[row], cols); buf[cols] = '\r'; buf[cols+1] = '\n'; buf[cols+2] = 0;
	reply(buf);
      }
      prompt();
    }
    break;

  case 'K': // inject keystrokes
    {
      const char *p = args;
      if (*p == ' ') p++; // one separating space after the K
      size_t inLen = strlen(p);
      uint16_t queued = g_debugger.injectKeys(p);
      // An exact count of the input's keystrokes would need the escapes
      // re-parsed; "fewer than we were given" is what a script cares about.
      bool full = (queued < inLen) && (g_debugger.injectQueueDepth() >= 256);
      replyf("Queued %u key%s%s (queue depth now %u)\r\n",
	     (unsigned)queued, queued == 1 ? "" : "s",
	     full ? " (queue full, remainder dropped)" : "",
	     (unsigned)g_debugger.injectQueueDepth());
      prompt();
    }
    break;

  case 'B':
    {
      const char *p = args;
      if (*p == ' ') p++;
      bool entered = false;
      if (!g_biosInterrupt) { g_biosInterrupt = true; entered = true; }
      unsigned queued = 0;
      while (*p) {
	uint8_t c;
	if (*p == '\\' && *(p+1)) {
	  p++;
	  switch (*p) {
	  case 'r': case 'n': c = PK_RET;  break;
	  case 't':           c = PK_TAB;  break;
	  case 'e':           c = PK_ESC;  break;
	  case 'U':           c = PK_UARR; break;
	  case 'D':           c = PK_DARR; break;
	  case 'L':           c = PK_LARR; break;
	  case 'R':           c = PK_RARR; break;
	  case 'd':           c = PK_DEL;  break;
	  case '\\':          c = '\\';   break;
	  case 'x':
	    if (*(p+1) && *(p+2)) {
	      c = (uint8_t)((HEXCHAR(*(p+1)) << 4) | HEXCHAR(*(p+2)));
	      p += 2;
	    } else {
	      c = 'x';
	    }
	    break;
	  default:            c = (uint8_t)*p; break;
	  }
	  p++;
	} else {
	  c = (uint8_t)*p++;
	}
	SDLKeyboard::injectBiosKey(c);
	queued++;
      }
      replyf("%s; queued %u BIOS key%s\r\n",
	     entered ? "Entering the BIOS" : "BIOS already up",
	     queued, queued == 1 ? "" : "s");
      prompt();
    }
    break;

  case 'P':
    {
      const char *p = args;
      while (*p == ' ') p++;
      if (!*p) {
	reply("P needs a path\r\n");
      } else if (((SDLDisplay *)g_display)->savePng(p)) {
	replyf("Wrote %s\r\n", p);
      } else {
	replyf("Could not write %s\r\n", p);
      }
      prompt();
    }
    break;

  case 'M': // the emulated mouse, for driving a mouse-only program from a
            // script. "M <x> <y>" puts the pointer at a position in the mouse
            // card's clamp space (whatever the program set up); "M +dx +dy"
            // moves it by that much, the way the host mouse does, which is
            // what a program reading the mouse in delta mode (GEOS parks it
            // mid-range every frame) needs. "M d" and "M u" press and
            // release the button; either form may end with d or u.
    {
      const char *p = args;
      while (*p == ' ') p++;
      char *end;
      bool relative = (*p == '+' || *p == '-');
      long x = strtol(p, &end, 10);
      if (end != p) {
	p = end;
	long y = strtol(p, &end, 10);
	if (end == p) { reply("M needs x and y\r\n"); prompt(); break; }
	p = end;
	if (relative) {
	  ((SDLMouse *)g_mouse)->gotMouseEvent(0, (int32_t)x, (int32_t)y);
	  replyf("Pointer moved by %ld,%ld\r\n", x, y);
	} else {
	  g_mouse->setPosition((uint16_t)x, (uint16_t)y);
	  replyf("Pointer at %ld,%ld\r\n", x, y);
	}
      }
      while (*p == ' ') p++;
      if (*p == 'd' || *p == 'u') {
	((SDLMouse *)g_mouse)->mouseButtonEvent(*p == 'd');
	replyf("Button %s\r\n", *p == 'd' ? "down" : "up");
      }
      prompt();
    }
    break;

  case '*': // one byte
    if (getAddress(args, &val)) {
      replyf("Memory location 0x%X: 0x%.2X\012\015", val,
	     g_debugger.peek((uint16_t)val, cpuBank));
    } else {
      reply("Invalid read\012\015");
    }
    prompt();
    break;

  case 'y': // cycle count and 1x-equivalent time
    replyf("cycles=%lld  t1x=%.3fs\012\015",
	   (long long)g_cpu->cycles,
	   (double)g_cpu->cycles / 1023000.0);
    prompt();
    break;

  case 'G': // set PC
    if (getAddress(args, &val)) {
      replyf("Setting PC to 0x%X\012\015", val);
      g_debugger.setPC((uint16_t)val);
    } else {
      reply("sscanf failed, skipping\012\015");
    }
    prompt();
    break;

  default:
    break;
  }
}
