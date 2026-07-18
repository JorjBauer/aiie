#ifndef __UNBACKEND_H
#define __UNBACKEND_H
#include <stdint.h>

/* Abstract host-network operations UserNet needs, so the same user-mode NAT
 * stack can run on SDL (BSD sockets) and, later, the Teensy (ESP socket
 * protocol). Handles are opaque non-negative ints; -1 means none. Every call is
 * non-blocking: a TCP connect is started and then polled for completion. */
class UnBackend {
 public:
  virtual ~UnBackend() {}

  // TCP client.
  virtual int  tcpOpen() = 0;                                             // handle or -1
  virtual bool tcpConnect(int h, const uint8_t ip[4], uint16_t port) = 0; // false = hard failure
  virtual int  tcpConnectPoll(int h) = 0;   // 1 connected, 0 pending, -1 failed
  virtual int  tcpSend(int h, const uint8_t *data, uint16_t len) = 0;     // bytes >=0, -1 error
  virtual int  tcpRecv(int h, uint8_t *buf, uint16_t maxLen) = 0;         // >0 data, 0 none, -1 closed
  virtual void tcpShutdownWrite(int h) = 0;

  // UDP. bindPort 0 means an ephemeral local port.
  virtual int  udpOpen(uint16_t bindPort) = 0;                           // handle or -1
  virtual int  udpSend(int h, const uint8_t ip[4], uint16_t port,
                       const uint8_t *data, uint16_t len) = 0;
  virtual int  udpRecv(int h, uint8_t *buf, uint16_t maxLen,
                       uint8_t srcIp[4], uint16_t *srcPort) = 0;          // >0 data, 0 none

  virtual void sockClose(int h) = 0;

  // A monotonic-ish seconds clock (for idle timeouts). Platform-provided so the
  // NAT stack needs no direct time() call.
  virtual uint32_t nowSecs() = 0;

  // Inbound host->guest forwarding (default unsupported; SDL implements it).
  virtual int  tcpListen(uint16_t hostPort) { (void)hostPort; return -1; }
  virtual int  tcpAccept(int listenH) { (void)listenH; return -1; }
};

#endif
