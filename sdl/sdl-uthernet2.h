#ifndef __SDL_UTHERNET2_H
#define __SDL_UTHERNET2_H

#include <stdint.h>
#include "uthernet2interface.h"
#include "usernet.h"
#include "usernet-bsd.h"

/* SDLUthernet2 carries Uthernet2 traffic over the host's own TCP/IP stack
 * using non-blocking Berkeley sockets. It needs no external hardware, which
 * makes it the back end for desktop testing.
 *
 * TCP (client and single-connection server) and UDP use host sockets. MAC-RAW
 * (own-stack software) is served by a built-in user-mode network, UserNet,
 * which answers the Apple's ARP/ICMP/DHCP and NATs its traffic to host sockets.
 * IP raw is not implemented; opening one leaves the socket CLOSED.
 */
class SDLUthernet2 : public Uthernet2Interface {
 public:
  SDLUthernet2();
  virtual ~SDLUthernet2();

  virtual void begin();
  virtual void reset();

  virtual void socketOpen(uint8_t sock, uint8_t proto, uint8_t ipproto,
                          uint16_t localPort);
  virtual void socketConnect(uint8_t sock, const uint8_t ip[4], uint16_t port);
  virtual void socketListen(uint8_t sock, uint16_t localPort);
  virtual void socketClose(uint8_t sock);
  virtual uint8_t socketStatus(uint8_t sock);

  virtual int socketSend(uint8_t sock, const uint8_t *data, uint16_t len,
                         const uint8_t destIp[4], uint16_t destPort);
  virtual int socketRecv(uint8_t sock, uint8_t *buf, uint16_t maxLen,
                         uint8_t srcIp[4], uint16_t *srcPort);

  virtual int sendRawFrame(const uint8_t *frame, uint16_t len);
  virtual int recvRawFrame(uint8_t *buf, uint16_t maxLen);

  virtual bool resolveName(const char *host, uint8_t ip[4]);
  virtual void tick(int64_t cycleCount);

 private:
  void serviceSocket(uint8_t s);

  // Inbound NAT: the emulated Apple picks its own listen port, but binding that
  // exact port on the host is a problem for privileged ports (<1024 need root)
  // and for ports already in use. This maps the Apple's listen port to the
  // host port we actually bind, so a normal (non-root) user can still reach an
  // Apple server. The Apple software is unchanged; only the host-facing port
  // differs.
  uint16_t mapInboundPort(uint16_t applePort) const;

  int      fd[U2_NUM_SOCKETS];        // -1 when unused
  uint8_t  status[U2_NUM_SOCKETS];    // U2_SR_*
  uint8_t  proto[U2_NUM_SOCKETS];     // U2_PROTO_*
  uint16_t localPort[U2_NUM_SOCKETS];      // Apple's requested TCP-server port
  uint16_t inboundOffset;                  // added to privileged Apple listen ports
  uint16_t boundHostPort[U2_NUM_SOCKETS];  // host port last bound for LISTEN (0=none)
  uint16_t boundApplePort[U2_NUM_SOCKETS]; // Apple port that host port was bound for

  // Locally-generated replies (built-in DHCP responder) handed to the Apple
  // ahead of any host-socket data. DHCP is answered here, not on the wire.
  uint8_t  pendData[U2_NUM_SOCKETS][300];
  uint16_t pendLen[U2_NUM_SOCKETS];        // 0 = nothing pending
  uint8_t  pendSrcIp[U2_NUM_SOCKETS][4];
  uint16_t pendSrcPort[U2_NUM_SOCKETS];

  // Built-in user-mode network for MAC-RAW mode (own-stack software).
  UnBackendBsd unBsd;      // declared before usernet: constructed first
  UserNet  usernet;
};

#endif
