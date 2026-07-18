#include "usernet-esp.h"
#include "protocol.h"
#include <string.h>

UnBackendEsp::UnBackendEsp(EspTransport *tr) : t(tr) {
  for (int i = 0; i < UNESP_SLOTS; i++) {
    slots[i].used = false; slots[i].proto = 0xFF; slots[i].sr = W5100_SR_CLOSED;
    slots[i].role = ROLE_FLOW; slots[i].lport = 0;
  }
}

// (Re)open a listener slot as a TCP server on its stored port.
bool UnBackendEsp::openListen(int h) {
  uint8_t rt; uint8_t rb[64]; uint16_t rl;
  uint8_t op[5] = { (uint8_t)h, AIIE_PROTO_TCP, 0, 0, 0 };
  if (!(t->espCommand(CMD_SOCK_OPEN, op, 5, rt, rb, sizeof(rb), rl, 200) && rt == EVT_SOCK_STATE))
    return false;
  uint8_t lp[3] = { (uint8_t)h, (uint8_t)(slots[h].lport & 0xFF), (uint8_t)(slots[h].lport >> 8) };
  if (!(t->espCommand(CMD_SOCK_LISTEN, lp, 3, rt, rb, sizeof(rb), rl, 200) &&
        rt == EVT_SOCK_STATE && rl >= 2)) return false;
  slots[h].proto = AIIE_PROTO_TCP;
  slots[h].sr = rb[1];  // LISTEN
  return true;
}

int UnBackendEsp::tcpListen(uint16_t hostPort) {
  int s = allocSlot();
  if (s < 0) return -1;
  slots[s].used = true; slots[s].lport = hostPort;
  if (!openListen(s)) { slots[s].used = false; return -1; }
  slots[s].role = ROLE_LISTEN;
  return s;
}

int UnBackendEsp::tcpAccept(int h) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  if (slots[h].role == ROLE_LISTEN) {
    poll(h, 0, 0, 0, 0);   // refresh status
    if (slots[h].sr == W5100_SR_ESTABLISHED) {
      slots[h].role = ROLE_LISTEN_CONN;  // the listener socket is now the connection
      return h;
    }
  }
  return -1;  // no client yet, or a connection is already in progress
}

int UnBackendEsp::allocSlot() {
  for (int i = 0; i < UNESP_SLOTS; i++) if (!slots[i].used) return i;
  return -1;  // all four ESP sockets busy: the flow is dropped, Apple retries
}

uint32_t UnBackendEsp::nowSecs() { return t->nowSecs(); }

int UnBackendEsp::tcpOpen() {
  int s = allocSlot();
  if (s < 0) return -1;
  uint8_t pl[5] = { (uint8_t)s, AIIE_PROTO_TCP, 0, 0, 0 };  // ipproto 0, lport 0
  uint8_t rt; uint8_t rb[64]; uint16_t rl;
  if (t->espCommand(CMD_SOCK_OPEN, pl, 5, rt, rb, sizeof(rb), rl, 200) &&
      rt == EVT_SOCK_STATE && rl >= 2) {
    slots[s].used = true; slots[s].proto = AIIE_PROTO_TCP; slots[s].sr = rb[1];
    slots[s].role = ROLE_FLOW;
    return s;
  }
  return -1;
}

bool UnBackendEsp::tcpConnect(int h, const uint8_t ip[4], uint16_t port) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return false;
  uint8_t pl[7] = { (uint8_t)h, ip[0], ip[1], ip[2], ip[3],
                    (uint8_t)(port & 0xFF), (uint8_t)(port >> 8) };
  uint8_t rt; uint8_t rb[64]; uint16_t rl;
  if (t->espCommand(CMD_SOCK_CONNECT, pl, 7, rt, rb, sizeof(rb), rl, 1200) &&
      rt == EVT_SOCK_STATE && rl >= 2) {
    slots[h].sr = rb[1];
    return slots[h].sr != W5100_SR_CLOSED;   // ESTABLISHED or SYNSENT is fine
  }
  return false;
}

int UnBackendEsp::poll(int h, uint8_t *buf, uint16_t maxLen,
                       uint8_t *srcIp, uint16_t *srcPort) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return 0;
  uint8_t pl[3] = { (uint8_t)h, (uint8_t)(maxLen & 0xFF), (uint8_t)(maxLen >> 8) };
  uint8_t rt; static uint8_t rb[AIIE_ESP_MAX_PAYLOAD]; uint16_t rl;
  if (!t->espCommand(CMD_SOCK_POLL, pl, 3, rt, rb, sizeof(rb), rl, 200)) return 0;
  if (rt != EVT_SOCK_DATA || rl < 5) return 0;
  slots[h].sr = rb[1];
  uint8_t flags = rb[4];
  uint16_t o = 5;
  if (flags & AIIE_DATA_HAS_SRC) {
    if (rl >= o + 6) {
      if (srcIp) memcpy(srcIp, rb + o, 4);
      if (srcPort) *srcPort = (uint16_t)rb[o + 4] | ((uint16_t)rb[o + 5] << 8);
      o += 6;
    }
  }
  uint16_t dlen = (rl > o) ? (rl - o) : 0;
  if (dlen > maxLen) dlen = maxLen;
  if (dlen && buf) memcpy(buf, rb + o, dlen);
  return (int)dlen;
}

int UnBackendEsp::tcpConnectPoll(int h) {
  poll(h, 0, 0, 0, 0);           // refresh status (no data taken)
  uint8_t sr = slots[h].sr;
  if (sr == W5100_SR_ESTABLISHED) return 1;
  if (sr == W5100_SR_CLOSED || sr == W5100_SR_CLOSE_WAIT) return -1;
  return 0;                       // SYNSENT / INIT: still connecting
}

int UnBackendEsp::tcpSend(int h, const uint8_t *data, uint16_t len) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  static uint8_t sbuf[AIIE_ESP_MAX_PAYLOAD];
  uint16_t o = 0;
  sbuf[o++] = (uint8_t)h;
  sbuf[o++] = 0;  // no dest (TCP)
  uint16_t room = AIIE_ESP_MAX_PAYLOAD - o;
  uint16_t n = (len > room) ? room : len;
  memcpy(sbuf + o, data, n); o += n;
  uint8_t rt; uint8_t rb[64]; uint16_t rl;
  if (t->espCommand(CMD_SOCK_SEND, sbuf, o, rt, rb, sizeof(rb), rl, 400) &&
      rt == EVT_SOCK_SENT && rl >= 3) {
    return (uint16_t)rb[1] | ((uint16_t)rb[2] << 8);
  }
  return -1;
}

int UnBackendEsp::tcpRecv(int h, uint8_t *buf, uint16_t maxLen) {
  int n = poll(h, buf, maxLen, 0, 0);
  if (n > 0) return n;
  uint8_t sr = slots[h].sr;
  if (sr == W5100_SR_CLOSE_WAIT || sr == W5100_SR_CLOSED) return -1;  // peer closed
  return 0;
}

void UnBackendEsp::tcpShutdownWrite(int) {
  // The ESP socket protocol has no half-close; a full close would also stop the
  // host's reply stream, so the Apple's FIN is not propagated as a half-close.
  // The flow closes fully via sockClose() once both sides are done.
}

int UnBackendEsp::udpOpen(uint16_t bindPort) {
  int s = allocSlot();
  if (s < 0) return -1;
  uint8_t pl[5] = { (uint8_t)s, AIIE_PROTO_UDP, 0,
                    (uint8_t)(bindPort & 0xFF), (uint8_t)(bindPort >> 8) };
  uint8_t rt; uint8_t rb[64]; uint16_t rl;
  if (t->espCommand(CMD_SOCK_OPEN, pl, 5, rt, rb, sizeof(rb), rl, 200) &&
      rt == EVT_SOCK_STATE && rl >= 2) {
    slots[s].used = true; slots[s].proto = AIIE_PROTO_UDP; slots[s].sr = rb[1];
    slots[s].role = ROLE_FLOW;
    return s;
  }
  return -1;
}

int UnBackendEsp::udpSend(int h, const uint8_t ip[4], uint16_t port,
                          const uint8_t *data, uint16_t len) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  static uint8_t sbuf[AIIE_ESP_MAX_PAYLOAD];
  uint16_t o = 0;
  sbuf[o++] = (uint8_t)h;
  sbuf[o++] = AIIE_SEND_HAS_DEST;
  memcpy(sbuf + o, ip, 4); o += 4;
  sbuf[o++] = (uint8_t)(port & 0xFF); sbuf[o++] = (uint8_t)(port >> 8);
  uint16_t room = AIIE_ESP_MAX_PAYLOAD - o;
  uint16_t n = (len > room) ? room : len;
  memcpy(sbuf + o, data, n); o += n;
  uint8_t rt; uint8_t rb[64]; uint16_t rl;
  if (t->espCommand(CMD_SOCK_SEND, sbuf, o, rt, rb, sizeof(rb), rl, 400) &&
      rt == EVT_SOCK_SENT && rl >= 3) {
    return (uint16_t)rb[1] | ((uint16_t)rb[2] << 8);
  }
  return -1;
}

int UnBackendEsp::udpRecv(int h, uint8_t *buf, uint16_t maxLen,
                          uint8_t srcIp[4], uint16_t *srcPort) {
  return poll(h, buf, maxLen, srcIp, srcPort);
}

void UnBackendEsp::sockClose(int h) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return;
  uint8_t pl[1] = { (uint8_t)h };
  uint8_t rt; uint8_t rb[64]; uint16_t rl;
  t->espCommand(CMD_SOCK_CLOSE, pl, 1, rt, rb, sizeof(rb), rl, 200);
  if (slots[h].role == ROLE_LISTEN_CONN) {
    // The forwarded connection closed: re-listen on the same slot/port so the
    // next client can reach the Apple server.
    if (openListen(h)) { slots[h].role = ROLE_LISTEN; return; }
  }
  slots[h].used = false; slots[h].proto = 0xFF; slots[h].sr = W5100_SR_CLOSED;
  slots[h].role = ROLE_FLOW;
}
