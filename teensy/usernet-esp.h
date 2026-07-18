#ifndef __USERNET_ESP_H
#define __USERNET_ESP_H

#include "unbackend.h"
#include "esptransport.h"

/* UnBackendEsp implements UserNet's host-network operations over the ESP socket
 * protocol (CMD_SOCK_*), so the same NAT stack that runs on SDL runs on the
 * Teensy with the ESP-01 doing the real WiFi. Handles are ESP socket indices
 * (0..ESP_SLOTS-1); the ESP has four hardware-style sockets, which in MAC-RAW
 * mode are all free for NAT flows. */
#define UNESP_SLOTS 4

class UnBackendEsp : public UnBackend {
 public:
  UnBackendEsp(EspTransport *t);

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

  // Inbound host->guest forwarding: the ESP listens on a WiFi port and each
  // accepted connection is spliced to the Apple server. The ESP has a single-
  // connection listen model (the listener socket becomes the connection on
  // accept), so a slot re-listens when its connection closes -> one connection
  // at a time per listener, which suffices for a simple webserver.
  virtual int tcpListen(uint16_t hostPort);
  virtual int tcpAccept(int listenH);

 private:
  int  allocSlot();
  bool openListen(int h);   // (re)open + LISTEN a listener slot on its lport
  // Poll one socket; caches status and returns any datagram/stream bytes.
  int  poll(int h, uint8_t *buf, uint16_t maxLen,
            uint8_t *srcIp, uint16_t *srcPort);

  // role: 0 = ordinary flow, 1 = listening, 2 = listener with a live connection.
  enum { ROLE_FLOW = 0, ROLE_LISTEN = 1, ROLE_LISTEN_CONN = 2 };
  EspTransport *t;
  struct Slot { bool used; uint8_t proto; uint8_t sr; uint8_t role; uint16_t lport; }
    slots[UNESP_SLOTS];
};

#endif
