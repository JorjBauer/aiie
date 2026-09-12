#ifndef __DEBUG_SOCKET_H
#define __DEBUG_SOCKET_H

#include <stdint.h>
#include <pthread.h>

// The SDL build's debugger front end: a TCP listener speaking the text
// protocol (c s S b d L D T K * y G h q Q, plus w W p) on top of the
// platform-free core in debugger.h.
//
// Threading: a listener thread accepts one client at a time and reads
// its commands, pushing each complete command on a queue. The emulator
// thread calls poll() once per loop iteration to run the queued commands,
// write their replies, and report any halt the core recorded. Nothing
// here blocks the emulator.
class DebugSocket {
 public:
  DebugSocket();

  // Start listening. Separate from the constructor because this is a
  // global, built before main() has seen argv. Port 0 means "do not
  // listen". A second call is ignored.
  void listenOn(uint16_t port);

  // Emulator thread, once per loop iteration.
  void poll();

  bool connected() { return cd != -1; }

  // ---- the listener thread's side; not for hosts ----
  void serve();

 private:
  struct Cmd { char *text; Cmd *next; };

  void push(const char *text, size_t len);
  Cmd *take();
  void handle(char *cmd);
  void reply(const char *s);
  void replyf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
  void prompt();
  void statusLine();
  void reportHalt();

  int sd;   // listener
  int cd;   // client, or -1
  pthread_t thread;
  pthread_mutex_t lock;
  Cmd *head, *tail;
  volatile bool newClient;      // set by the listener when a client connects
  volatile bool clientClosed;   // set by the listener when the client went away
};

extern DebugSocket g_debugSocket;

#endif
