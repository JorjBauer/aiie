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
  breakpoint = 0;

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
}

Debugger::~Debugger()
{
}

bool getAddress(const char *buf, unsigned int *addrOut)
{
  unsigned int val;
  if (sscanf(buf, " 0x%X", &val) == 1) {
    *addrOut = val;
    return true;
  } else if (sscanf(buf, " $%X", &val) == 1) {
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

    uint8_t cmdbuf[50];

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


    if (breakpoint && g_cpu->pc != breakpoint) {
      // Running until we reach the breakpoint
      return;
    }

    uint8_t b; // byte value used in parsing
    unsigned int val; // common value buffer used in parsing
    
    GETCH;
    snprintf(buf, sizeof(buf), "Got char %d\012\015", b);
    write(cd, buf, strlen(buf));

    switch (b) {
    case 'c': // Continue - close connection and let execution flow
      printf("Closing debugging socket\n");
      close(cd); cd=-1;
      break;
    case 'b': // Set breakpoint
      GETLN;
      if (getAddress(buf, &val)) {
	breakpoint = val;
      } else {
	breakpoint = 0;
      }
      if (breakpoint) {
	snprintf(buf, sizeof(buf), "Breakpoint set to 0x%X\012\015", breakpoint);
      } else {
	snprintf(buf, sizeof(buf), "Breakpoint removed\012\015");
      }
      write(cd, buf, strlen(buf));
      break;

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
      break;

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
}


void Debugger::setSocket(int fd)
{
  printf("New debugger session established\n");
  cd = fd;
}

bool Debugger::active()
{
  return (cd != -1);
}
