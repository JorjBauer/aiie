#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include <pthread.h>

class Debugger {
 public:
  Debugger();
  ~Debugger();

  void setSocket(int cliSock);
  void step();

  // private:
  int sd; // server (listener)
  int cd; // client (connected to us)
  pthread_t listenThreadID;

};


#endif
