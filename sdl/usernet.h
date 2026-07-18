#ifndef __USERNET_H
#define __USERNET_H

#include <stdint.h>

/* UserNet is a small user-mode network for W5100 MAC-RAW mode. In MAC-RAW the
 * Apple runs its own TCP/IP stack and emits whole Ethernet frames; there is no
 * real network behind the emulator, so this synthesizes one. It answers the
 * Apple's ARP, ICMP echo, and DHCP locally, presenting a QEMU-style virtual LAN
 * (client 10.0.2.15, gateway/services 10.0.2.2, DNS 10.0.2.3), and NATs the
 * Apple's outbound TCP and UDP to ordinary host sockets: a TCP connection is
 * spliced (we are the Apple's TCP peer, a host socket is the real peer), and
 * UDP datagrams are relayed, with DNS rewritten to a real resolver.
 *
 * No host privileges and no real interface are needed.
 */

#define USERNET_MAXFRAME 1522   // one Ethernet frame (with a little VLAN margin)
#define USERNET_QUEUE    32     // frames buffered toward the Apple
#define USERNET_FLOWS    32     // concurrent NAT flows (TCP + UDP)
#define USERNET_FWDS     4      // host->guest port-forward listeners
#define USERNET_TCP_MSS  1460

// A host->guest port forward: a host TCP listener whose connections are spliced
// to the Apple acting as a server (QEMU hostfwd). Configured via the env var
// AIIE_USERNET_HOSTFWD, e.g. "8080:80" (host 127.0.0.1:8080 -> Apple :80).
struct UnFwd {
  int      lfd;         // host listening socket, -1 when unused
  uint16_t applePort;   // the Apple server port to forward to
};

// NAT flow: one Apple<->host conversation.
struct UnFlow {
  bool     used;
  uint8_t  proto;          // 6 TCP, 17 UDP
  uint8_t  appleIp[4];
  uint16_t applePort;
  uint8_t  dstIp[4];       // as the Apple addressed it (10.0.2.3 for DNS)
  uint16_t dstPort;
  uint8_t  realIp[4];      // where we actually send (resolver for DNS)
  int      fd;             // host socket
  // TCP splice state
  uint8_t  state;          // UN_TCP_*
  uint32_t rcvNext;        // next seq expected from the Apple (our ACK value)
  uint32_t sndNext;        // next seq we will send to the Apple
  uint32_t sndUna;         // oldest unacked seq (last ACK from the Apple)
  uint16_t appleWin;       // the Apple's advertised receive window
  bool     finRcvd;        // Apple sent FIN
  bool     finSent;        // we sent FIN toward the Apple
  uint32_t lastActive;     // ms, for idle timeout
};

class UserNet {
 public:
  UserNet();
  ~UserNet();
  void reset();

  // One outbound Ethernet frame from the Apple. May enqueue reply frames.
  void fromApple(const uint8_t *frame, uint16_t len);
  // Hand one queued frame to the Apple; returns its length (0 if none waiting).
  uint16_t toApple(uint8_t *buf, uint16_t maxLen);
  // Service host sockets: pull inbound data/close, complete connects, time out.
  void tick();

 private:
  // link-layer / local services
  void handleArp(const uint8_t *f, uint16_t len);
  void handleIp(const uint8_t *f, uint16_t len);
  void handleIcmp(const uint8_t *f, uint16_t len, const uint8_t *ipHdr);
  void handleUdp(const uint8_t *f, uint16_t len, const uint8_t *ipHdr);
  void handleTcp(const uint8_t *f, uint16_t len, const uint8_t *ipHdr);
  void handleDhcp(const uint8_t *udpPayload, uint16_t plen);

  // NAT flow management + host I/O
  UnFlow *findFlow(uint8_t proto, const uint8_t *aip, uint16_t aport,
                   const uint8_t *dip, uint16_t dport);
  UnFlow *allocFlow();
  void    closeFlow(UnFlow *f);
  void    serviceTcp(UnFlow *f);
  void    serviceUdp(UnFlow *f);

  // host->guest port forwarding (the Apple as a server)
  void    setupListeners();
  void    acceptInbound();
  void    appleServerIp(uint8_t out[4]) const;

  // frame builders (toward the Apple)
  void queueFrame(const uint8_t *frame, uint16_t len);
  bool queueHasRoom() const;
  uint16_t ethHeader(uint8_t *out, const uint8_t *dstMac, uint16_t ethertype);
  void sendTcp(UnFlow *f, uint8_t flags, const uint8_t *data, uint16_t dlen);
  void sendUdpToApple(const uint8_t *srcIp, uint16_t srcPort,
                      const uint8_t *dstIp, uint16_t dstPort,
                      const uint8_t *data, uint16_t dlen);

  uint8_t  appleMac[6];
  bool     haveAppleMac;
  uint8_t  appleIp[4];     // the Apple's IP, learned from its outbound frames
  bool     haveAppleIp;
  bool     dbg;            // trace frames when AIIE_USERNET_DEBUG is set
  uint32_t isnCounter;     // hands out TCP initial sequence numbers
  uint16_t inbEphem;       // next synthetic client port for inbound connections

  UnFlow   flows[USERNET_FLOWS];
  UnFwd    fwds[USERNET_FWDS];

  // Ring of frames waiting for the Apple to pull.
  uint8_t  q[USERNET_QUEUE][USERNET_MAXFRAME];
  uint16_t qlen[USERNET_QUEUE];
  uint8_t  qHead, qTail;
};

#endif
