#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include <pthread.h>
#include <inttypes.h>

class Debugger {
 public:
  Debugger();
  ~Debugger();

  void setSocket(int cliSock);
  void step();
  bool active();

  // private:
  int sd; // server (listener)
  int cd; // client (connected to us)
  pthread_t listenThreadID;

  uint32_t breakpoint;
  bool steppingOut;
  bool singleStep;
};


#endif
