#include "debugger.h"
#include "globals.h"
#include "disassembler.h"

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
  struct sockaddr_in server;
  int optval;

  sd = socket(AF_INET, SOCK_STREAM, 0);
  cd = -1;
  removeAllBreakpoints();

  history = NULL;
  endh = NULL;
  historyCount = 0;

  optval=1;
  setsockopt(sd, SOL_SOCKET, SO_REUSEADDR,
	     (void*)&optval, sizeof(optval));

  memset(&server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = INADDR_ANY;
  server.sin_port = htons(12345);

  steppingOut = false;
  singleStep = false;

  if (bind(sd, (struct sockaddr *) &server, sizeof(server)) < 0) {
    perror("error binding to debug socket");
    exit(1);
  }

  listen(sd,5);


  if (!pthread_create(&listenThreadID, NULL, &cpu_thread, (void *)this)) {
    ; // ... what?
  }
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

#define GETCH { if ((read(cd,&b,1)) == -1) { close(cd); cd=-1; return; } }

#define GETLN {   int ptr=0;   while (((read(cd,&b,1)) != -1) && ptr < sizeof(buf) && b != 10 && b != 13) {   if (b) {buf[ptr++] = b;}   }   buf[ptr]=0; }

#define HEXCHAR(x) ((x>='0'&&x<='9')?x-'0':(x>='a'&&x<='f')?x-'a'+10:(x>='A'&&x<='F')?x-'A'+10:(x=='i' || x=='I')?1:(x=='o' || x=='O')?0:0)
#define FROMHEXP(p) ((HEXCHAR(*p) << 4) | HEXCHAR(*(p+1)))

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
    sprintf(buf, "debug [$%X]> ", g_cpu->pc);
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
	     b != 'h' && // show history
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
	  sprintf(buf, "%d ", i++);
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
      
    case '*': // read 1 byte of memory. Use '* 0x<address>'
      {
	GETLN;
	if (getAddress(buf, &val)) {
	  sprintf(buf, "Memory location 0x%X: ", val);
	  write(cd, buf, strlen(buf));
	  val = g_vm->getMMU()->read(val);
	  sprintf(buf, "0x%.2X\012\015", val);
	  write(cd, buf, strlen(buf));
	} else {
	  sprintf(buf, "Invalid read\012\015");
	  write(cd, buf, strlen(buf));
	}
      }
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
  // FIXME snprintf
  sprintf(&buf[strlen(buf)], " ;; OP: $%02x A: %02x  X: %02x  Y: %02x  PC: $%04x SP: %02x S: %.2x Flags: %c%cx%c%c%c%c%c\012\015",
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
