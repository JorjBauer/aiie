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

void Debugger::step()
{
  static char buf[256];

  if (cd != -1) {
    bzero(buf,256);
    int n = read( cd,buf,255 );

    if (n < 0) {
      // error
      close(cd);
      cd = -1;
      return;
    }
    
    if (n > 0) {
      if (buf[0] == 'c') {
	// Continue - close connection
	close(cd);
	return;
      }
      // ... ?
      //   b - set breakpoint
      //   s - step over
      //   S - step out
      //   c - continue (close connection)
      //   d - disassemble @ current PC
    }

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
    write(cd, buf, strlen(buf));

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
  }
}


void Debugger::setSocket(int fd)
{
  cd = fd;
}
