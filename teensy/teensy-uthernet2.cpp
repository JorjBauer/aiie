#include "teensy-uthernet2.h"
#include "protocol.h"
#include <string.h>

TeensyUthernet2 *TeensyUthernet2::s_instance = nullptr;

void TeensyUthernet2::frameCb(uint8_t type, uint8_t seq, const uint8_t *p, uint16_t len)
{
  if (s_instance) s_instance->onFrame(type, seq, p, len);
}

void TeensyUthernet2::onFrame(uint8_t type, uint8_t seq, const uint8_t *p, uint16_t len)
{
  rpType = type;
  rpSeq = seq;
  rpLen = (len > sizeof(rpBuf)) ? sizeof(rpBuf) : len;
  memcpy(rpBuf, p, rpLen);
  rpGot = true;
}

TeensyUthernet2::TeensyUthernet2(Stream *l, const char *hostfwd)
  : link(l), parser(frameCb), unEsp(this), usernet(&unEsp, false, hostfwd),
    macraw(false)
{
  s_instance = this;
  linkUp = false;
  lastProbe = 0;
  seq = 0;
  cTx = 0; cRetries = 0; cTimeouts = 0;
  rpGot = false; rpType = 0; rpSeq = 0; rpLen = 0;
  pollCursor = 0;
  haveCreds = false;
  ssid[0] = 0; pass[0] = 0;
  for (uint8_t i = 0; i < U2_NUM_SOCKETS; i++) {
    sr[i] = U2_SR_CLOSED;
    proto[i] = 0xFF;
    rxLen[i] = 0; rxOff[i] = 0;
    rxHasSrc[i] = false; rxSrcPort[i] = 0;
  }
}

TeensyUthernet2::~TeensyUthernet2()
{
  if (s_instance == this) s_instance = nullptr;
}

void TeensyUthernet2::setNetwork(const char *s, const char *p)
{
  strncpy(ssid, s ? s : "", sizeof(ssid) - 1); ssid[sizeof(ssid) - 1] = 0;
  strncpy(pass, p ? p : "", sizeof(pass) - 1); pass[sizeof(pass) - 1] = 0;
  haveCreds = ssid[0] != 0;
}

uint8_t TeensyUthernet2::nextSeq()
{
  seq++;
  if (seq == 0) seq = 1;
  return seq;
}

bool TeensyUthernet2::command(uint8_t type, const uint8_t *payload, uint16_t len,
                              uint32_t timeoutMs)
{
  // Reuse ONE seq across retries: the ESP dedups by seq, so a resend either
  // gets reprocessed (if the first never arrived) or re-fetches the cached
  // reply (if only the reply was lost), never double-executing the command.
  const uint8_t s = nextSeq();

  for (uint8_t attempt = 0; attempt <= TU2_MAX_RETRIES; attempt++) {
    if (attempt) cRetries++;
    rpGot = false;
    frameSend(*link, type, s, payload, len);
    cTx++;

    const uint32_t start = millis();
    while ((uint32_t)(millis() - start) < timeoutMs) {
      while (link->available()) {
        parser.feed((uint8_t)link->read());
        if (rpGot && rpSeq == s) return true;
      }
      yield();
    }
  }
  cTimeouts++;
  return false;
}

// EspTransport: one command round-trip, reply copied out for UnBackendEsp.
bool TeensyUthernet2::espCommand(uint8_t type, const uint8_t *payload, uint16_t len,
                                 uint8_t &rType, uint8_t *rBuf, uint16_t rCap,
                                 uint16_t &rLen, uint32_t timeoutMs)
{
  if (!command(type, payload, len, timeoutMs)) return false;
  rType = rpType;
  rLen = (rpLen > rCap) ? rCap : rpLen;
  memcpy(rBuf, rpBuf, rLen);
  return true;
}

uint32_t TeensyUthernet2::nowSecs() { return millis() / 1000; }

int TeensyUthernet2::sendRawFrame(const uint8_t *frame, uint16_t len)
{
  usernet.fromApple(frame, len);
  return len;
}

int TeensyUthernet2::recvRawFrame(uint8_t *buf, uint16_t maxLen)
{
  return (int)usernet.toApple(buf, maxLen);
}

uint32_t TeensyUthernet2::statFramesSent()     { return cTx; }
uint32_t TeensyUthernet2::statFramesReceived() { return parser.framesOk; }
uint32_t TeensyUthernet2::statCrcErrors()      { return parser.crcErrors; }
uint32_t TeensyUthernet2::statRetries()        { return cRetries; }
uint32_t TeensyUthernet2::statTimeouts()       { return cTimeouts; }

void TeensyUthernet2::begin()
{
  linkUp = false;
  // Probe the link: the ESP is reply-only, so poll it until it answers. Keep
  // this short so an absent or unpowered ESP does not stall boot for long.
  uint8_t echo = 0x5A;
  for (int i = 0; i < 10 && !linkUp; i++) {
    if (command(CMD_LINK_PING, &echo, 1, 60)) linkUp = true;
  }
  if (linkUp && haveCreds) {
    uint8_t buf[1 + 32 + 1 + 64];
    uint16_t o = 0;
    uint8_t sl = strlen(ssid); if (sl > 32) sl = 32;
    uint8_t pl = strlen(pass); if (pl > 64) pl = 64;
    buf[o++] = sl; memcpy(buf + o, ssid, sl); o += sl;
    buf[o++] = pl; memcpy(buf + o, pass, pl); o += pl;
    command(CMD_WIFI_JOIN, buf, o, 400);
  }
}

void TeensyUthernet2::reset()
{
  for (uint8_t i = 0; i < U2_NUM_SOCKETS; i++) {
    sr[i] = U2_SR_CLOSED;
    proto[i] = 0xFF;
    rxLen[i] = 0; rxOff[i] = 0;
  }
  macraw = false;
  usernet.reset();
  command(CMD_RESET, nullptr, 0);
}

bool TeensyUthernet2::linkReady()
{
  return linkUp;
}

void TeensyUthernet2::socketOpen(uint8_t sock, uint8_t p, uint8_t ipproto,
                                 uint16_t localPort)
{
  if (sock >= U2_NUM_SOCKETS) return;
  sr[sock] = U2_SR_CLOSED;
  proto[sock] = 0xFF;
  rxLen[sock] = 0; rxOff[sock] = 0;

  // MAC-RAW is not a hardware socket on the ESP: the on-Teensy UserNet handles
  // the Apple's frames and NATs them to the ESP's sockets.
  if (p == U2_PROTO_MACRAW) {
    usernet.reset();
    macraw = true;
    sr[sock] = U2_SR_MACRAW;
    proto[sock] = p;
    return;
  }

  uint8_t pl[5] = { sock, p, ipproto,
                    (uint8_t)(localPort & 0xFF), (uint8_t)(localPort >> 8) };
  if (command(CMD_SOCK_OPEN, pl, 5) && rpType == EVT_SOCK_STATE && rpLen >= 2) {
    sr[sock] = rpBuf[1];
    proto[sock] = p;
  }
}

void TeensyUthernet2::socketConnect(uint8_t sock, const uint8_t ip[4], uint16_t port)
{
  if (sock >= U2_NUM_SOCKETS) return;
  uint8_t pl[7] = { sock, ip[0], ip[1], ip[2], ip[3],
                    (uint8_t)(port & 0xFF), (uint8_t)(port >> 8) };
  if (command(CMD_SOCK_CONNECT, pl, 7) && rpType == EVT_SOCK_STATE && rpLen >= 2) {
    sr[sock] = rpBuf[1];
  }
}

void TeensyUthernet2::socketListen(uint8_t sock, uint16_t localPort)
{
  if (sock >= U2_NUM_SOCKETS) return;
  uint8_t pl[3] = { sock, (uint8_t)(localPort & 0xFF), (uint8_t)(localPort >> 8) };
  if (command(CMD_SOCK_LISTEN, pl, 3) && rpType == EVT_SOCK_STATE && rpLen >= 2) {
    sr[sock] = rpBuf[1];
  }
}

void TeensyUthernet2::socketClose(uint8_t sock)
{
  if (sock >= U2_NUM_SOCKETS) return;
  uint8_t pl[1] = { sock };
  command(CMD_SOCK_CLOSE, pl, 1);
  sr[sock] = U2_SR_CLOSED;
  proto[sock] = 0xFF;
  rxLen[sock] = 0; rxOff[sock] = 0;
}

uint8_t TeensyUthernet2::socketStatus(uint8_t sock)
{
  if (sock >= U2_NUM_SOCKETS) return U2_SR_CLOSED;
  return sr[sock];
}

int TeensyUthernet2::socketSend(uint8_t sock, const uint8_t *data, uint16_t len,
                                const uint8_t destIp[4], uint16_t destPort)
{
  if (sock >= U2_NUM_SOCKETS) return 0;

  static uint8_t buf[AIIE_ESP_MAX_PAYLOAD];
  uint16_t o = 0;
  uint8_t flags = (proto[sock] == U2_PROTO_UDP) ? AIIE_SEND_HAS_DEST : 0;
  buf[o++] = sock;
  buf[o++] = flags;
  if (flags & AIIE_SEND_HAS_DEST) {
    memcpy(buf + o, destIp, 4); o += 4;
    buf[o++] = (uint8_t)(destPort & 0xFF);
    buf[o++] = (uint8_t)(destPort >> 8);
  }
  uint16_t room = AIIE_ESP_MAX_PAYLOAD - o;
  uint16_t n = (len > room) ? room : len;
  memcpy(buf + o, data, n); o += n;

  if (command(CMD_SOCK_SEND, buf, o) && rpType == EVT_SOCK_SENT && rpLen >= 3) {
    return (uint16_t)rpBuf[1] | ((uint16_t)rpBuf[2] << 8);
  }
  return 0;
}

int TeensyUthernet2::socketRecv(uint8_t sock, uint8_t *buf, uint16_t maxLen,
                                uint8_t srcIp[4], uint16_t *srcPort)
{
  if (sock >= U2_NUM_SOCKETS || rxLen[sock] == 0 || maxLen == 0) return 0;

  if (proto[sock] == U2_PROTO_UDP || proto[sock] == U2_PROTO_IPRAW) {
    // Datagram-oriented: deliver the whole record or nothing.
    uint16_t avail = rxLen[sock];
    if (maxLen < avail) return 0;
    memcpy(buf, rxData[sock], avail);
    if (srcIp && rxHasSrc[sock]) memcpy(srcIp, rxSrcIp[sock], 4);
    if (srcPort) *srcPort = rxSrcPort[sock];
    rxLen[sock] = 0; rxOff[sock] = 0;
    return avail;
  }

  // TCP stream: deliver up to maxLen from the buffer.
  uint16_t avail = rxLen[sock] - rxOff[sock];
  uint16_t n = (avail > maxLen) ? maxLen : avail;
  memcpy(buf, rxData[sock] + rxOff[sock], n);
  rxOff[sock] += n;
  if (rxOff[sock] >= rxLen[sock]) { rxLen[sock] = 0; rxOff[sock] = 0; }
  return n;
}

bool TeensyUthernet2::resolveName(const char *host, uint8_t ip[4])
{
  if (!linkUp || !host) return false;
  uint16_t hl = strlen(host);
  if (hl == 0 || hl > 127) return false;

  if (!command(CMD_DNS_RESOLVE, (const uint8_t *)host, hl, 300)) return false;

  for (int i = 0; i < 50; i++) {
    if (command(CMD_DNS_RESULT, nullptr, 0, 200) &&
        rpType == EVT_DNS && rpLen >= 5) {
      if (rpBuf[0] == 0) { // no longer pending
        ip[0] = rpBuf[1]; ip[1] = rpBuf[2]; ip[2] = rpBuf[3]; ip[3] = rpBuf[4];
        return (ip[0] | ip[1] | ip[2] | ip[3]) != 0;
      }
    }
    delay(20);
  }
  return false;
}

void TeensyUthernet2::tick(int64_t cycleCount)
{
  (void)cycleCount;
  if (!linkUp) {
    // Retry the link occasionally so it recovers if the ESP came up late.
    if ((uint32_t)(millis() - lastProbe) > 3000) {
      lastProbe = millis();
      uint8_t echo = 0x5A;
      if (command(CMD_LINK_PING, &echo, 1, 60)) linkUp = true;
    }
    return;
  }

  // MAC-RAW mode: the on-Teensy UserNet services its NAT flows (each flow polls
  // its ESP socket). The hardware-socket path below is not used in this mode.
  if (macraw) { usernet.tick(); return; }

  // Service at most one active socket per tick (round-robin), to bound the
  // half-duplex round-trip cost per maintenance call.
  for (uint8_t k = 0; k < U2_NUM_SOCKETS; k++) {
    uint8_t s = (pollCursor + k) % U2_NUM_SOCKETS;
    if (proto[s] == 0xFF) continue;
    pollCursor = (s + 1) % U2_NUM_SOCKETS;

    if (rxLen[s] != 0) return; // still draining this socket's buffer

    uint16_t maxlen = TU2_RXBUF;
    uint8_t pl[3] = { s, (uint8_t)(maxlen & 0xFF), (uint8_t)(maxlen >> 8) };
    if (command(CMD_SOCK_POLL, pl, 3) && rpType == EVT_SOCK_DATA && rpLen >= 5) {
      sr[s] = rpBuf[1];
      uint8_t flags = rpBuf[4];
      uint16_t o = 5;
      if (flags & AIIE_DATA_HAS_SRC) {
        if (rpLen >= o + 6) {
          memcpy(rxSrcIp[s], rpBuf + o, 4); o += 4;
          rxSrcPort[s] = (uint16_t)rpBuf[o] | ((uint16_t)rpBuf[o + 1] << 8); o += 2;
          rxHasSrc[s] = true;
        }
      } else {
        rxHasSrc[s] = false;
      }
      uint16_t dlen = (rpLen > o) ? (rpLen - o) : 0;
      if (dlen > TU2_RXBUF) dlen = TU2_RXBUF;
      if (dlen) memcpy(rxData[s], rpBuf + o, dlen);
      rxLen[s] = dlen;
      rxOff[s] = 0;
    }
    return; // one socket per tick
  }
}
