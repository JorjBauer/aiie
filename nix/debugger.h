#ifndef __DEBUGGER_H
#define __DEBUGGER_H

#include <pthread.h>
#include <inttypes.h>

#define MAX_BREAKPOINTS 32
#define MAX_HISTORY 10000

struct _history {
  char *msg;
  struct _history *next;
};

class Debugger {
 public:
  Debugger();
  ~Debugger();

  // START LISTENING. Separate from the constructor because the Debugger
  // is a global, so it is built before main() has seen argv: the port
  // has to be handed to it afterwards. Port 0 means "do not listen",
  // which is the simplest way for one of several instances to stay out
  // of the way. Safe to call once; a second call is ignored.
  void listenOn(uint16_t port);

  void setSocket(int cliSock);
  void step();
  bool active();

  bool addBreakpoint(uint16_t addr);
  bool isAnyBreakpointSet();
  bool isBreakpointAt(uint16_t addr);
  bool removeBreakpoint(uint16_t addr);
  void removeAllBreakpoints();

  void addStringToHistory(const char *s);
  void addCurrentPCToHistory();

  // private:
  int sd; // server (listener)
  int cd; // client (connected to us)
  pthread_t listenThreadID;

  uint32_t breakpoints[MAX_BREAKPOINTS];
  bool steppingOut;
  bool singleStep;

  struct _history *history;
  struct _history *endh;
  uint32_t historyCount;
};


#endif
