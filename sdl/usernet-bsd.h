#ifndef __USERNET_BSD_H
#define __USERNET_BSD_H

#include "unbackend.h"

/* UnBackendBsd implements UserNet's host-network operations with ordinary
 * non-blocking Berkeley sockets. Handles are the socket fds. This is the SDL
 * (desktop) back end; the Teensy uses a different UnBackend that speaks to the
 * ESP co-processor. */
class UnBackendBsd : public UnBackend {
 public:
  virtual int  tcpOpen();
  virtual bool tcpConnect(int h, const uint8_t ip[4], uint16_t port);
  virtual int  tcpConnectPoll(int h);
  virtual int  tcpSend(int h, const uint8_t *data, uint16_t len);
  virtual int  tcpRecv(int h, uint8_t *buf, uint16_t maxLen);
  virtual void tcpShutdownWrite(int h);

  virtual int  udpOpen(uint16_t bindPort);
  virtual int  udpSend(int h, const uint8_t ip[4], uint16_t port,
                       const uint8_t *data, uint16_t len);
  virtual int  udpRecv(int h, uint8_t *buf, uint16_t maxLen,
                       uint8_t srcIp[4], uint16_t *srcPort);

  virtual void sockClose(int h);
  virtual uint32_t nowSecs();

  virtual int  tcpListen(uint16_t hostPort);
  virtual int  tcpAccept(int listenH);
};

#endif
