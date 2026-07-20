#include "usernet.h"
#include <string.h>
#include <stdarg.h>

// Portable debug trace: writes to stderr on the host, no-op on the Teensy
// (where stderr does not exist). Guarded by the per-instance dbg flag anyway.
#ifdef TEENSYDUINO
static inline void unLog(const char *, ...) {}
#else
#include <stdio.h>
static void unLog(const char *fmt, ...) {
  va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
#endif

/* ---- the virtual LAN --------------------------------------------------- *
 * A QEMU-style user network. The Apple is the client; everything else here
 * is synthesized. Services (gateway, DNS, DHCP) answer from OUR_MAC.
 */
static const uint8_t OUR_MAC[6]   = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x02 };
static const uint8_t CLIENT_IP[4] = { 10, 0, 2, 15 };
static const uint8_t GW_IP[4]     = { 10, 0, 2, 2 };   // gateway + DHCP server
static const uint8_t DNS_IP[4]    = { 10, 0, 2, 3 };   // advertised resolver
static const uint8_t MASK[4]      = { 255, 255, 255, 0 };
static const uint8_t BCAST_MAC[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static const uint8_t BCAST_IP[4]  = { 255, 255, 255, 255 };
static const uint8_t RESOLVER[4]  = { 8, 8, 8, 8 };    // real DNS behind DNS_IP

#define ETH_ARP  0x0806
#define ETH_IPV4 0x0800
#define IP_ICMP  1
#define IP_TCP   6
#define IP_UDP   17

// TCP flags.
#define TH_FIN 0x01
#define TH_SYN 0x02
#define TH_RST 0x04
#define TH_PSH 0x08
#define TH_ACK 0x10

// Flow states.
#define UN_FREE        0
#define UN_TCP_CONN    1  // outbound: host connect() in progress; SYN-ACK unsent
#define UN_TCP_SYNACK  2  // outbound: SYN-ACK sent, awaiting the Apple's ACK
#define UN_TCP_EST     3  // established
#define UN_TCP_FIN     4  // Apple half-closed (its FIN seen)
#define UN_UDP         5
#define UN_TCP_ISYN    6  // inbound: SYN sent to the Apple, awaiting its SYN-ACK

#define SYN_TIMEOUT_SECS 4

#define UDP_IDLE_SECS 60

/* ---- byte helpers (network order) -------------------------------------- */
static inline uint16_t rd16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }
static inline uint32_t rd32(const uint8_t *p) {
  return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}
static inline void wr16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = v & 0xFF; }
static inline void wr32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
static inline bool seqLT(uint32_t a, uint32_t b) { return (int32_t)(a - b) < 0; }
static inline bool seqLE(uint32_t a, uint32_t b) { return (int32_t)(a - b) <= 0; }

static uint16_t checksum16(const uint8_t *data, uint16_t len, uint32_t sum) {
  while (len > 1) { sum += rd16(data); data += 2; len -= 2; }
  if (len) sum += (uint16_t)data[0] << 8; // last odd byte in the high position
  while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
  return (uint16_t)~sum;
}
static uint16_t ipChecksum(const uint8_t *ipHdr, uint16_t ihl) {
  return checksum16(ipHdr, ihl, 0);
}
// Pseudo-header partial sum for TCP/UDP checksums.
static uint32_t pseudoSum(const uint8_t *src, const uint8_t *dst,
                          uint8_t proto, uint16_t len) {
  return rd16(src) + rd16(src + 2) + rd16(dst) + rd16(dst + 2) + proto + len;
}

static bool inSubnet(const uint8_t *ip) {
  for (int i = 0; i < 4; i++)
    if ((ip[i] & MASK[i]) != (CLIENT_IP[i] & MASK[i])) return false;
  return true;
}
static bool isOurIp(const uint8_t *ip) {
  return !memcmp(ip, GW_IP, 4) || !memcmp(ip, DNS_IP, 4);
}

// Where a destination the Apple addressed actually lives on the host side.
// Following the QEMU user-net convention: the gateway (10.0.2.2) is the host
// itself (reachable at 127.0.0.1), and the advertised DNS (10.0.2.3) is a real
// resolver. Everything else is used as-is (the real internet).
static void mapRealIp(const uint8_t *dip, uint8_t *realIp) {
  static const uint8_t LOOPBACK[4] = { 127, 0, 0, 1 };
  if (!memcmp(dip, GW_IP, 4))       memcpy(realIp, LOOPBACK, 4);
  else if (!memcmp(dip, DNS_IP, 4)) memcpy(realIp, RESOLVER, 4);
  else                              memcpy(realIp, dip, 4);
}

UserNet::UserNet(UnBackend *b, bool debug, const char *hostfwd) : backend(b) {
  for (int i = 0; i < USERNET_FLOWS; i++) flows[i].fd = -1;
  for (int i = 0; i < USERNET_FWDS; i++) fwds[i].lfd = -1;
  dbg = debug;
  reset();
  setupListeners(hostfwd); // host listeners persist across card resets
}

uint32_t UserNet::nowSecs() { return backend->nowSecs(); }
UserNet::~UserNet() {
  for (int i = 0; i < USERNET_FLOWS; i++) if (flows[i].fd >= 0) backend->sockClose(flows[i].fd);
  for (int i = 0; i < USERNET_FWDS; i++) if (fwds[i].lfd >= 0) backend->sockClose(fwds[i].lfd);
}

void UserNet::reset() {
  haveAppleMac = false;
  memset(appleMac, 0, sizeof(appleMac));
  haveAppleIp = false;
  memset(appleIp, 0, sizeof(appleIp));
  isnCounter = 0x1000;
  inbEphem = 40000;
  qHead = qTail = 0;
  for (int i = 0; i < USERNET_QUEUE; i++) qlen[i] = 0;
  for (int i = 0; i < USERNET_FLOWS; i++) {
    if (flows[i].fd >= 0) backend->sockClose(flows[i].fd);
    memset(&flows[i], 0, sizeof(flows[i]));
    flows[i].fd = -1;
    flows[i].state = UN_FREE;
  }
}

// The Apple's server address for inbound forwards: what we learned from its
// traffic, falling back to the DHCP lease it would have taken.
void UserNet::appleServerIp(uint8_t out[4]) const {
  if (haveAppleIp) memcpy(out, appleIp, 4);
  else             memcpy(out, CLIENT_IP, 4);
}

// Parse AIIE_USERNET_HOSTFWD ("hostport:appleport[,hostport:appleport...]") and
// open a host TCP listener on 127.0.0.1 for each rule.
void UserNet::setupListeners(const char *cfg) {
  if (!cfg) return;
  uint8_t n = 0;
  const char *p = cfg;
  while (*p && n < USERNET_FWDS) {
    int hp = 0, ap = 0;
    while (*p >= '0' && *p <= '9') hp = hp * 10 + (*p++ - '0');
    if (*p != ':') break;
    p++;
    while (*p >= '0' && *p <= '9') ap = ap * 10 + (*p++ - '0');
    if (hp > 0 && hp < 65536 && ap > 0 && ap < 65536) {
      int s = backend->tcpListen((uint16_t)hp);
      if (s >= 0) {
        fwds[n].lfd = s; fwds[n].applePort = (uint16_t)ap; n++;
        unLog("Uthernet: host forward 127.0.0.1:%d -> Apple :%d\n", hp, ap);
      } else {
        // Do not fail silently: a busy or privileged host port is the usual
        // reason an inbound forward "does nothing".
        unLog("Uthernet: cannot open host forward port %d\n", hp);
      }
    }
    if (*p == ',') p++; else break;
  }
}

void UserNet::reconfigureForwards(const char *cfg) {
  // Tear down the current listeners so a shrunk or changed list does not leave
  // stale ports bound, then re-open from the new config. setupListeners refills
  // fwds[] from slot 0; the closed tail keeps lfd == -1 and is skipped on accept.
  for (int i = 0; i < USERNET_FWDS; i++) {
    if (fwds[i].lfd >= 0) { backend->sockClose(fwds[i].lfd); fwds[i].lfd = -1; }
  }
  setupListeners(cfg);
}

/* ---- frame queue toward the Apple -------------------------------------- */
bool UserNet::queueHasRoom() const {
  return (uint8_t)((qTail + 1) % USERNET_QUEUE) != qHead;
}
void UserNet::queueFrame(const uint8_t *frame, uint16_t len) {
  if (len > USERNET_MAXFRAME || !queueHasRoom()) return;
  memcpy(q[qTail], frame, len);
  qlen[qTail] = len;
  qTail = (uint8_t)((qTail + 1) % USERNET_QUEUE);
}
uint16_t UserNet::toApple(uint8_t *buf, uint16_t maxLen) {
  if (qHead == qTail) return 0;
  uint16_t len = qlen[qHead];
  if (len > maxLen) len = maxLen;
  memcpy(buf, q[qHead], len);
  qHead = (uint8_t)((qHead + 1) % USERNET_QUEUE);
  return len;
}
uint16_t UserNet::ethHeader(uint8_t *out, const uint8_t *dstMac, uint16_t ethertype) {
  memcpy(out, dstMac, 6);
  memcpy(out + 6, OUR_MAC, 6);
  wr16(out + 12, ethertype);
  return 14;
}

/* ---- inbound (from the Apple) ------------------------------------------ */
void UserNet::fromApple(const uint8_t *frame, uint16_t len) {
  if (len < 14) return;
  if (!haveAppleMac) { memcpy(appleMac, frame + 6, 6); haveAppleMac = true; }
  uint16_t ethertype = rd16(frame + 12);
  if (dbg) {
    if (ethertype == ETH_ARP && len >= 42)
      unLog("[un] < ARP who-has %u.%u.%u.%u\n",
              frame[38], frame[39], frame[40], frame[41]);
    else if (ethertype == ETH_IPV4 && len >= 34) {
      const uint8_t *ip = frame + 14; uint16_t ihl = (ip[0] & 0xF) * 4;
      const uint8_t *l4 = ip + ihl;
      unLog("[un] < IP proto=%u %u.%u.%u.%u -> %u.%u.%u.%u",
              ip[9], ip[12], ip[13], ip[14], ip[15], ip[16], ip[17], ip[18], ip[19]);
      if (ip[9] == IP_TCP) unLog(" tcp :%u->:%u flags=0x%02X",
              rd16(l4), rd16(l4 + 2), l4[13]);
      else if (ip[9] == IP_UDP) unLog(" udp :%u->:%u", rd16(l4), rd16(l4 + 2));
      unLog("\n");
    } else unLog("[un] < ethertype 0x%04X\n", ethertype);
  }
  if (ethertype == ETH_ARP)       handleArp(frame, len);
  else if (ethertype == ETH_IPV4) handleIp(frame, len);
}

void UserNet::handleArp(const uint8_t *f, uint16_t len) {
  if (len < 14 + 28) return;
  const uint8_t *a = f + 14;
  if (rd16(a) != 1 || rd16(a + 2) != ETH_IPV4) return;
  if (rd16(a + 6) != 1) return;                        // request only
  const uint8_t *spa = a + 14;
  const uint8_t *tpa = a + 24;
  if (!inSubnet(tpa) || !memcmp(tpa, CLIENT_IP, 4)) return;

  uint8_t out[14 + 28];
  ethHeader(out, f + 6, ETH_ARP);
  uint8_t *r = out + 14;
  wr16(r, 1); wr16(r + 2, ETH_IPV4); r[4] = 6; r[5] = 4; wr16(r + 6, 2); // reply
  memcpy(r + 8, OUR_MAC, 6);
  memcpy(r + 14, tpa, 4);
  memcpy(r + 18, f + 6, 6);
  memcpy(r + 24, spa, 4);
  queueFrame(out, sizeof(out));
}

void UserNet::handleIp(const uint8_t *f, uint16_t len) {
  if (len < 14 + 20) return;
  const uint8_t *ip = f + 14;
  if ((ip[0] >> 4) != 4) return;
  uint16_t ihl = (ip[0] & 0x0F) * 4;
  if (ihl < 20 || 14 + ihl > len) return;
  // Learn the Apple's address so inbound forwards know where to send (its DHCP
  // frames have a 0.0.0.0 source, so only take a real in-subnet address).
  const uint8_t *src = ip + 12;
  if ((src[0] | src[1] | src[2] | src[3]) && inSubnet(src)) {
    memcpy(appleIp, src, 4); haveAppleIp = true;
  }
  switch (ip[9]) {
  case IP_ICMP: handleIcmp(f, len, ip); break;
  case IP_UDP:  handleUdp(f, len, ip);  break;
  case IP_TCP:  handleTcp(f, len, ip);  break;
  }
}

void UserNet::handleIcmp(const uint8_t *f, uint16_t len, const uint8_t *ip) {
  uint16_t ihl = (ip[0] & 0x0F) * 4;
  const uint8_t *dst = ip + 16;
  if (!isOurIp(dst)) return;                 // external ping would need raw sockets
  const uint8_t *icmp = ip + ihl;
  uint16_t icmpLen = rd16(ip + 2) - ihl;
  if (14 + ihl + 8 > len || icmpLen < 8) return;
  if (icmp[0] != 8) return;                  // echo request

  uint8_t out[USERNET_MAXFRAME];
  uint16_t o = ethHeader(out, f + 6, ETH_IPV4);
  if (o + ihl + icmpLen > USERNET_MAXFRAME) return;
  uint8_t *oip = out + o;
  memcpy(oip, ip, ihl);
  oip[8] = 64;
  memcpy(oip + 12, ip + 16, 4);
  memcpy(oip + 16, ip + 12, 4);
  wr16(oip + 10, 0); wr16(oip + 10, ipChecksum(oip, ihl));
  uint8_t *oicmp = oip + ihl;
  memcpy(oicmp, icmp, icmpLen);
  oicmp[0] = 0;
  wr16(oicmp + 2, 0); wr16(oicmp + 2, checksum16(oicmp, icmpLen, 0));
  queueFrame(out, o + ihl + icmpLen);
}

/* ---- UDP: DHCP locally, everything else NATed -------------------------- */
void UserNet::handleUdp(const uint8_t *f, uint16_t len, const uint8_t *ip) {
  uint16_t ihl = (ip[0] & 0x0F) * 4;
  const uint8_t *udp = ip + ihl;
  if (14 + ihl + 8 > len) return;
  uint16_t sport = rd16(udp);
  uint16_t dport = rd16(udp + 2);
  uint16_t ulen = rd16(udp + 4);
  if (ulen < 8 || 14 + ihl + ulen > len) return;

  if (dport == 67) { handleDhcp(udp + 8, ulen - 8); return; }

  // NAT this datagram out to a host UDP socket.
  const uint8_t *aip = ip + 12;
  const uint8_t *dip = ip + 16;
  UnFlow *fl = findFlow(IP_UDP, aip, sport, dip, dport);
  if (!fl) {
    fl = allocFlow();
    if (!fl) return;
    fl->proto = IP_UDP; fl->state = UN_UDP;
    memcpy(fl->appleIp, aip, 4); fl->applePort = sport;
    memcpy(fl->dstIp, dip, 4);   fl->dstPort = dport;
    mapRealIp(dip, fl->realIp);
    fl->fd = backend->udpOpen(0);
    if (fl->fd < 0) { closeFlow(fl); return; }
  }
  fl->lastActive = nowSecs();
  backend->udpSend(fl->fd, fl->realIp, dport, udp + 8, ulen - 8);
}

void UserNet::handleDhcp(const uint8_t *req, uint16_t plen) {
  if (plen < 240) return;
  if (req[0] != 1) return;
  if (rd16(req + 236) != 0x6382 || rd16(req + 238) != 0x5363) return; // cookie

  uint8_t msgType = 0;
  uint16_t i = 240;
  while (i + 1 < plen) {
    uint8_t opt = req[i];
    if (opt == 255) break;
    if (opt == 0) { i++; continue; }
    uint8_t olen = req[i + 1];
    if (opt == 53 && olen >= 1) msgType = req[i + 2];
    i += 2 + olen;
  }
  uint8_t reply;
  if (msgType == 1) reply = 2;        // DISCOVER -> OFFER
  else if (msgType == 3) reply = 5;   // REQUEST  -> ACK
  else return;

  uint8_t bp[300];
  memset(bp, 0, sizeof(bp));
  bp[0] = 2; bp[1] = 1; bp[2] = 6;
  memcpy(bp + 4, req + 4, 4);         // xid
  memcpy(bp + 16, CLIENT_IP, 4);      // yiaddr
  memcpy(bp + 20, GW_IP, 4);          // siaddr
  memcpy(bp + 28, req + 28, 6);       // chaddr
  wr16(bp + 236, 0x6382); wr16(bp + 238, 0x5363);
  uint16_t p = 240;
  bp[p++] = 53; bp[p++] = 1; bp[p++] = reply;
  bp[p++] = 54; bp[p++] = 4; memcpy(bp + p, GW_IP, 4); p += 4;
  bp[p++] = 51; bp[p++] = 4; wr16(bp + p, 0x0001); wr16(bp + p + 2, 0x5180); p += 4;
  bp[p++] = 1;  bp[p++] = 4; memcpy(bp + p, MASK, 4); p += 4;
  bp[p++] = 3;  bp[p++] = 4; memcpy(bp + p, GW_IP, 4); p += 4;
  bp[p++] = 6;  bp[p++] = 4; memcpy(bp + p, DNS_IP, 4); p += 4;
  bp[p++] = 255;
  // Broadcast the reply so a client with no IP yet accepts it (RFC 2131).
  sendUdpToApple(GW_IP, 67, BCAST_IP, 68, bp, p);
}

/* ---- UDP builder toward the Apple -------------------------------------- */
void UserNet::sendUdpToApple(const uint8_t *srcIp, uint16_t srcPort,
                             const uint8_t *dstIp, uint16_t dstPort,
                             const uint8_t *data, uint16_t dlen) {
  uint8_t out[USERNET_MAXFRAME];
  // A datagram to the all-ones address goes to the L2 broadcast too (this is how
  // a DHCP OFFER/ACK reaches a client that has no configured IP yet).
  bool bcast = !memcmp(dstIp, BCAST_IP, 4);
  uint16_t fo = ethHeader(out, bcast ? BCAST_MAC : (haveAppleMac ? appleMac : BCAST_MAC),
                          ETH_IPV4);
  uint16_t udpLen = 8 + dlen;
  uint16_t ipLen = 20 + udpLen;
  if (fo + ipLen > USERNET_MAXFRAME) return;

  uint8_t *ip = out + fo;
  memset(ip, 0, 20);
  ip[0] = 0x45; wr16(ip + 2, ipLen); ip[8] = 64; ip[9] = IP_UDP;
  memcpy(ip + 12, srcIp, 4); memcpy(ip + 16, dstIp, 4);
  wr16(ip + 10, 0); wr16(ip + 10, ipChecksum(ip, 20));

  uint8_t *udp = ip + 20;
  wr16(udp, srcPort); wr16(udp + 2, dstPort); wr16(udp + 4, udpLen); wr16(udp + 6, 0);
  memcpy(udp + 8, data, dlen);
  uint32_t sum = pseudoSum(srcIp, dstIp, IP_UDP, udpLen);
  wr16(udp + 6, checksum16(udp, udpLen, sum));
  if (rd16(udp + 6) == 0) wr16(udp + 6, 0xFFFF);
  queueFrame(out, fo + ipLen);
}

/* ---- TCP splice -------------------------------------------------------- */
void UserNet::sendTcp(UnFlow *f, uint8_t flags, const uint8_t *data, uint16_t dlen) {
  uint8_t out[USERNET_MAXFRAME];
  uint16_t fo = ethHeader(out, haveAppleMac ? appleMac : BCAST_MAC, ETH_IPV4);
  uint16_t thl = (flags & TH_SYN) ? 24 : 20;   // SYN carries an MSS option
  uint16_t ipLen = 20 + thl + dlen;
  if (fo + ipLen > USERNET_MAXFRAME) return;

  uint8_t *ip = out + fo;
  memset(ip, 0, 20);
  ip[0] = 0x45; wr16(ip + 2, ipLen); ip[8] = 64; ip[9] = IP_TCP;
  memcpy(ip + 12, f->dstIp, 4);        // src = the host the Apple addressed
  memcpy(ip + 16, f->appleIp, 4);      // dst = the Apple
  wr16(ip + 10, 0); wr16(ip + 10, ipChecksum(ip, 20));

  uint8_t *tcp = ip + 20;
  memset(tcp, 0, thl);
  wr16(tcp, f->dstPort); wr16(tcp + 2, f->applePort);
  wr32(tcp + 4, f->sndNext);
  wr32(tcp + 8, f->rcvNext);
  tcp[12] = (uint8_t)((thl / 4) << 4);
  tcp[13] = flags;
  wr16(tcp + 14, 0xFFFF);              // we drain promptly: advertise a big window
  if (flags & TH_SYN) { tcp[20] = 2; tcp[21] = 4; wr16(tcp + 22, USERNET_TCP_MSS); }
  if (dlen) memcpy(tcp + thl, data, dlen);

  uint32_t sum = pseudoSum(f->dstIp, f->appleIp, IP_TCP, thl + dlen);
  wr16(tcp + 16, checksum16(tcp, thl + dlen, sum));
  queueFrame(out, fo + ipLen);

  if (flags & TH_SYN) f->sndNext += 1;
  if (flags & TH_FIN) f->sndNext += 1;
  f->sndNext += dlen;
}

void UserNet::handleTcp(const uint8_t *f, uint16_t len, const uint8_t *ip) {
  uint16_t ihl = (ip[0] & 0x0F) * 4;
  uint16_t ipTotal = rd16(ip + 2);
  if (14 + ihl + 20 > len) return;
  const uint8_t *tcp = ip + ihl;
  uint16_t sport = rd16(tcp);
  uint16_t dport = rd16(tcp + 2);
  uint32_t seq = rd32(tcp + 4);
  uint32_t ack = rd32(tcp + 8);
  uint8_t  flags = tcp[13];
  uint16_t thl = (tcp[12] >> 4) * 4;
  uint16_t win = rd16(tcp + 14);
  if (14 + ihl + thl > len || thl < 20) return;
  const uint8_t *payload = tcp + thl;
  uint16_t paylen = (ipTotal > ihl + thl) ? (ipTotal - ihl - thl) : 0;
  const uint8_t *aip = ip + 12;
  const uint8_t *dip = ip + 16;

  UnFlow *fl = findFlow(IP_TCP, aip, sport, dip, dport);

  // A fresh SYN (no ACK) opens a connection.
  if ((flags & TH_SYN) && !(flags & TH_ACK)) {
    if (fl) {
      // Retransmitted SYN: still connecting -> wait; already answered -> re-send.
      if (fl->state == UN_TCP_SYNACK) sendTcp(fl, TH_SYN | TH_ACK, 0, 0);
      return;
    }
    fl = allocFlow();
    if (!fl) return;
    fl->proto = IP_TCP;
    memcpy(fl->appleIp, aip, 4); fl->applePort = sport;
    memcpy(fl->dstIp, dip, 4);   fl->dstPort = dport;
    mapRealIp(dip, fl->realIp);
    fl->rcvNext = seq + 1;             // SYN consumes one sequence number
    fl->sndNext = fl->sndUna = (isnCounter += 0xFA00);
    fl->appleWin = win;
    fl->finRcvd = fl->finSent = false;
    fl->lastActive = nowSecs();
    fl->fd = backend->tcpOpen();
    if (fl->fd < 0) { closeFlow(fl); return; }
    if (dbg) unLog("[un]   connect %u.%u.%u.%u:%u\n",
                     fl->realIp[0], fl->realIp[1], fl->realIp[2], fl->realIp[3], dport);
    if (!backend->tcpConnect(fl->fd, fl->realIp, dport)) {
      sendTcp(fl, TH_RST | TH_ACK, 0, 0); // refused/unreachable: reset the Apple
      closeFlow(fl);
      return;
    }
    // Always defer the SYN-ACK until the connect completes (serviceTcp polls),
    // so the Apple does not send data before the real connection exists.
    fl->state = UN_TCP_CONN;
    return;
  }

  if (!fl || fl->state == UN_FREE) {
    return; // stray segment for an unknown flow: ignore
  }
  fl->appleWin = win;
  fl->lastActive = nowSecs();

  // Inbound handshake: the Apple (acting as a server) answers our SYN.
  if ((flags & TH_SYN) && (flags & TH_ACK) && fl->state == UN_TCP_ISYN) {
    fl->rcvNext = seq + 1;         // its SYN consumes one sequence number
    fl->sndUna = ack;
    fl->state = UN_TCP_EST;
    sendTcp(fl, TH_ACK, 0, 0);     // complete the handshake; data can now flow
    return;
  }

  if (flags & TH_RST) { closeFlow(fl); return; }
  if (flags & TH_ACK) {
    if (seqLT(fl->sndUna, ack) || fl->sndUna == ack) fl->sndUna = ack;
    if (fl->state == UN_TCP_SYNACK) fl->state = UN_TCP_EST;
  }

  // Inbound data: write the in-order portion to the host socket, then ACK.
  if (paylen && (fl->state == UN_TCP_EST || fl->state == UN_TCP_FIN)) {
    uint32_t segEnd = seq + paylen;
    if (seqLE(seq, fl->rcvNext) && seqLT(fl->rcvNext, segEnd)) {
      uint32_t off = fl->rcvNext - seq;
      const uint8_t *p = payload + off;
      uint16_t n = (uint16_t)(paylen - off);
      if (fl->fd >= 0) {
        int w = backend->tcpSend(fl->fd, p, n);
        if (w > 0) fl->rcvNext += (uint32_t)w;
      }
    }
    sendTcp(fl, TH_ACK, 0, 0); // ack current cumulative position (or dup-ack)
  }

  // Apple closing its half.
  if ((flags & TH_FIN) && seq + paylen == fl->rcvNext && !fl->finRcvd) {
    fl->rcvNext += 1;              // FIN consumes one sequence number
    fl->finRcvd = true;
    if (fl->fd >= 0) backend->tcpShutdownWrite(fl->fd);
    if (fl->state == UN_TCP_EST) fl->state = UN_TCP_FIN;
    sendTcp(fl, TH_ACK, 0, 0);
  }

  // Both sides finished and our FIN is acknowledged: release the flow.
  if (fl->finRcvd && fl->finSent && fl->sndUna == fl->sndNext) closeFlow(fl);
}

/* ---- NAT flow table ---------------------------------------------------- */
UnFlow *UserNet::findFlow(uint8_t proto, const uint8_t *aip, uint16_t aport,
                          const uint8_t *dip, uint16_t dport) {
  for (int i = 0; i < USERNET_FLOWS; i++) {
    UnFlow &f = flows[i];
    if (f.state == UN_FREE || f.proto != proto) continue;
    if (f.applePort == aport && f.dstPort == dport &&
        !memcmp(f.appleIp, aip, 4) && !memcmp(f.dstIp, dip, 4))
      return &f;
  }
  return nullptr;
}
UnFlow *UserNet::allocFlow() {
  for (int i = 0; i < USERNET_FLOWS; i++)
    if (flows[i].state == UN_FREE) { memset(&flows[i], 0, sizeof(flows[i])); flows[i].fd = -1; return &flows[i]; }
  return nullptr; // table full: drop (the Apple's stack will retry)
}
void UserNet::closeFlow(UnFlow *f) {
  if (f->fd >= 0) backend->sockClose(f->fd);
  memset(f, 0, sizeof(*f));
  f->fd = -1;
  f->state = UN_FREE;
}

/* ---- host-socket servicing (host -> Apple) ----------------------------- */
// Accept new host connections on the port-forward listeners and open a spliced
// connection to the Apple server for each.
void UserNet::acceptInbound() {
  for (int i = 0; i < USERNET_FWDS; i++) {
    if (fwds[i].lfd < 0) continue;
    int c = backend->tcpAccept(fwds[i].lfd);
    if (c < 0) continue;
    UnFlow *fl = allocFlow();
    if (!fl) { backend->sockClose(c); continue; }
    fl->proto = IP_TCP;
    fl->fd = c;
    appleServerIp(fl->appleIp);          // our SYN's destination = the Apple
    fl->applePort = fwds[i].applePort;
    memcpy(fl->dstIp, GW_IP, 4);          // apparent client = the gateway (host)
    if (inbEphem < 40000) inbEphem = 40000;
    fl->dstPort = inbEphem++;
    fl->sndNext = fl->sndUna = (isnCounter += 0xFA00);
    fl->rcvNext = 0;
    fl->appleWin = 4096;                  // provisional until the Apple's SYN-ACK
    fl->finRcvd = fl->finSent = false;
    fl->state = UN_TCP_ISYN;
    fl->lastActive = nowSecs();
    if (dbg) unLog("[un] inbound accept -> SYN to %u.%u.%u.%u:%u from :%u\n",
                     fl->appleIp[0], fl->appleIp[1], fl->appleIp[2], fl->appleIp[3],
                     fl->applePort, fl->dstPort);
    sendTcp(fl, TH_SYN, 0, 0);            // SYN toward the Apple server
  }
}

void UserNet::serviceTcp(UnFlow *f) {
  if (f->state == UN_TCP_ISYN) {
    // Waiting for the Apple server to answer our SYN. If it never does (no
    // server listening), give up so the host connection drops.
    if (nowSecs() - f->lastActive > SYN_TIMEOUT_SECS) closeFlow(f);
    return;
  }
  if (f->state == UN_TCP_CONN) {
    // Poll the host connect; only answer the Apple's SYN once it actually
    // completes, so the Apple does not send data into a half-open connection.
    int cs = backend->tcpConnectPoll(f->fd);
    if (cs == 0) return;                            // still connecting
    if (cs < 0) {
      if (dbg) unLog("[un]   connect failed\n");
      sendTcp(f, TH_RST | TH_ACK, 0, 0);  // refused/unreachable: reset the Apple
      closeFlow(f); return;
    }
    f->state = UN_TCP_SYNACK;
    if (dbg) unLog("[un]   connect complete, SYN-ACK sent\n");
    sendTcp(f, TH_SYN | TH_ACK, 0, 0);              // real connection is up
    return;
  }
  if (f->state == UN_TCP_SYNACK) return; // wait for the Apple's handshake ACK
  if (f->state != UN_TCP_EST && f->state != UN_TCP_FIN) return;
  if (f->fd < 0) return;

  // Send host data to the Apple, bounded by its advertised window and our queue.
  uint32_t inflight = f->sndNext - f->sndUna;
  if (f->appleWin > inflight && queueHasRoom()) {
    uint32_t room = f->appleWin - inflight;
    if (room > USERNET_TCP_MSS) room = USERNET_TCP_MSS;
    uint8_t buf[USERNET_TCP_MSS];
    int n = backend->tcpRecv(f->fd, buf, (uint16_t)room);
    if (n > 0) {
      sendTcp(f, TH_PSH | TH_ACK, buf, (uint16_t)n);
      f->lastActive = nowSecs();
    } else if (n < 0 && !f->finSent) {
      sendTcp(f, TH_FIN | TH_ACK, 0, 0);   // host closed: FIN toward the Apple
      f->finSent = true;
    }
  }
  if (f->finRcvd && f->finSent && f->sndUna == f->sndNext) closeFlow(f);
}

void UserNet::serviceUdp(UnFlow *f) {
  if (f->fd < 0) return;
  uint8_t buf[1472];
  uint8_t sip[4]; uint16_t sport;
  for (int i = 0; i < 8 && queueHasRoom(); i++) {
    int n = backend->udpRecv(f->fd, buf, sizeof(buf), sip, &sport);
    if (n <= 0) break;
    // Reply appears to come from the address the Apple sent to (DNS_IP for DNS).
    sendUdpToApple(f->dstIp, f->dstPort, f->appleIp, f->applePort, buf, (uint16_t)n);
    f->lastActive = nowSecs();
  }
  if (nowSecs() - f->lastActive > UDP_IDLE_SECS) closeFlow(f);
}

void UserNet::tick() {
  acceptInbound();
  for (int i = 0; i < USERNET_FLOWS; i++) {
    UnFlow &f = flows[i];
    if (f.state == UN_FREE) continue;
    if (f.proto == IP_TCP) serviceTcp(&f);
    else if (f.proto == IP_UDP) serviceUdp(&f);
  }
}
