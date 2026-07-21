#include "usernet-esp.h"
#include "protocol.h"
#include <string.h>

UnBackendEsp::UnBackendEsp(EspTransport *tr) : t(tr), schedCursor(0) {
  for (int i = 0; i < UNESP_SLOTS; i++) freeSlot(i);
}

uint32_t UnBackendEsp::nowSecs() { return t->nowSecs(); }

void UnBackendEsp::freeSlot(int h) {
  Slot &s = slots[h];
  s.used = false; s.proto = 0xFF; s.sr = W5100_SR_CLOSED;
  s.role = ROLE_FLOW; s.phase = PH_FREE; s.op = OP_NONE; s.lport = 0;
  s.wantConnect = false; s.connectFailed = false;
  s.nextPollMs = 0; s.pollIvMs = UNESP_POLL_MIN_MS;
  s.rxLen = 0; s.rxOff = 0; s.rxHasSrc = false; s.rxSrcPort = 0;
  s.txLen = 0; s.txHasDest = false; s.txDestPort = 0;
}

int UnBackendEsp::allocSlot() {
  for (int i = 0; i < UNESP_SLOTS; i++) if (!slots[i].used) return i;
  return -1;  // all four ESP sockets busy: the flow is dropped, the Apple retries
}

void UnBackendEsp::reset() {
  for (int i = 0; i < UNESP_SLOTS; i++) freeSlot(i);
}

bool UnBackendEsp::debugSlot(uint8_t &phase, uint8_t &sr, uint16_t &rxLen, uint16_t &txLen) const {
  // Prefer a TCP slot over a lingering UDP (DNS) one, which would otherwise mask
  // the connection of interest.
  int pick = -1;
  for (int i = 0; i < UNESP_SLOTS; i++) {
    if (!slots[i].used) continue;
    if (slots[i].proto == AIIE_PROTO_TCP) { pick = i; break; }
    if (pick < 0) pick = i;
  }
  if (pick < 0) return false;
  phase = slots[pick].phase; sr = slots[pick].sr;
  rxLen = slots[pick].rxLen; txLen = slots[pick].txLen;
  return true;
}

// ---- non-blocking backend surface --------------------------------------------

int UnBackendEsp::tcpOpen() {
  int s = allocSlot();
  if (s < 0) return -1;
  freeSlot(s);
  slots[s].used = true; slots[s].proto = AIIE_PROTO_TCP;
  slots[s].role = ROLE_FLOW; slots[s].phase = PH_OPEN;
  return s;   // OPEN (and any CONNECT) happen asynchronously in service()
}

bool UnBackendEsp::tcpConnect(int h, const uint8_t ip[4], uint16_t port) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return false;
  memcpy(slots[h].connIp, ip, 4);
  slots[h].connPort = port;
  slots[h].wantConnect = true;
  slots[h].connectFailed = false;
  return true;   // the scheduler OPENs (if needed) then CONNECTs
}

int UnBackendEsp::tcpConnectPoll(int h) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  Slot &s = slots[h];
  if (s.connectFailed) return -1;
  if (s.sr == W5100_SR_ESTABLISHED) return 1;
  if (s.phase < PH_READY) return 0;                 // still opening/connecting
  if (s.sr == W5100_SR_CLOSED || s.sr == W5100_SR_CLOSE_WAIT) return -1;
  return 0;                                          // SYNSENT / INIT: connecting
}

int UnBackendEsp::tcpRecv(int h, uint8_t *buf, uint16_t maxLen) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  Slot &s = slots[h];
  if (s.rxLen > 0) {
    uint16_t n = (s.rxLen < maxLen) ? s.rxLen : maxLen;
    if (buf && n) memcpy(buf, s.rx + s.rxOff, n);
    s.rxOff += n; s.rxLen -= n;
    return (int)n;
  }
  // Only report a closed peer once the connection actually reached READY; before
  // that the (still-CLOSED) status just means we haven't finished connecting.
  if (s.phase >= PH_READY &&
      (s.sr == W5100_SR_CLOSE_WAIT || s.sr == W5100_SR_CLOSED)) return -1;
  return 0;   // nothing buffered yet; service() will poll for more
}

int UnBackendEsp::tcpSend(int h, const uint8_t *data, uint16_t len) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  Slot &s = slots[h];
  if (s.txLen > 0) return 0;    // a send is still staged/in-flight: backpressure
  uint16_t n = (len > UNESP_TXBUF) ? UNESP_TXBUF : len;
  if (n) memcpy(s.tx, data, n);
  s.txLen = n; s.txHasDest = false;
  return (int)n;                // service() will push it to the ESP
}

void UnBackendEsp::tcpShutdownWrite(int) {
  // The ESP socket protocol has no half-close; a full close would also stop the
  // host's reply stream. The flow closes fully via sockClose() once both sides
  // are done.
}

int UnBackendEsp::udpOpen(uint16_t bindPort) {
  int s = allocSlot();
  if (s < 0) return -1;
  freeSlot(s);
  slots[s].used = true; slots[s].proto = AIIE_PROTO_UDP;
  slots[s].role = ROLE_FLOW; slots[s].phase = PH_OPEN; slots[s].lport = bindPort;
  return s;
}

int UnBackendEsp::udpSend(int h, const uint8_t ip[4], uint16_t port,
                          const uint8_t *data, uint16_t len) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  Slot &s = slots[h];
  if (s.txLen > 0) return 0;    // previous datagram not sent yet: backpressure
  uint16_t n = (len > UNESP_TXBUF) ? UNESP_TXBUF : len;
  if (n) memcpy(s.tx, data, n);
  s.txLen = n; s.txHasDest = true;
  memcpy(s.txDestIp, ip, 4); s.txDestPort = port;
  return (int)n;
}

int UnBackendEsp::udpRecv(int h, uint8_t *buf, uint16_t maxLen,
                          uint8_t srcIp[4], uint16_t *srcPort) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return 0;
  Slot &s = slots[h];
  if (s.rxLen == 0) return 0;   // no datagram buffered; service() will poll
  uint16_t n = (s.rxLen < maxLen) ? s.rxLen : maxLen;
  if (buf && n) memcpy(buf, s.rx + s.rxOff, n);
  if (s.rxHasSrc) {
    if (srcIp) memcpy(srcIp, s.rxSrcIp, 4);
    if (srcPort) *srcPort = s.rxSrcPort;
  }
  s.rxLen = 0; s.rxOff = 0; s.rxHasSrc = false;   // a datagram is consumed whole
  return (int)n;
}

void UnBackendEsp::sockClose(int h) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return;
  slots[h].phase = PH_CLOSE;   // the scheduler issues CMD_SOCK_CLOSE, then frees
}

int UnBackendEsp::tcpListen(uint16_t hostPort) {
  int s = allocSlot();
  if (s < 0) return -1;
  freeSlot(s);
  slots[s].used = true; slots[s].proto = AIIE_PROTO_TCP;
  slots[s].role = ROLE_LISTEN; slots[s].phase = PH_OPEN; slots[s].lport = hostPort;
  return s;
}

int UnBackendEsp::tcpAccept(int h) {
  if (h < 0 || h >= UNESP_SLOTS || !slots[h].used) return -1;
  if (slots[h].role == ROLE_LISTEN && slots[h].sr == W5100_SR_ESTABLISHED) {
    slots[h].role = ROLE_LISTEN_CONN;   // the listener socket is now the connection
    return h;
  }
  return -1;   // no client yet
}

// ---- scheduler ---------------------------------------------------------------

// Issue the one command this slot needs next (returns false if it needs nothing
// or the engine refused it). Priority within a READY slot: drain staged TX, then
// poll for RX / status.
bool UnBackendEsp::issueFor(int h) {
  Slot &s = slots[h];
  switch (s.phase) {
    case PH_OPEN: {
      uint16_t lp = (s.proto == AIIE_PROTO_UDP) ? s.lport : 0;
      uint8_t op[5] = { (uint8_t)h, s.proto, 0,
                        (uint8_t)(lp & 0xFF), (uint8_t)(lp >> 8) };
      if (t->espIssue(CMD_SOCK_OPEN, op, 5, 200, (uint8_t)h)) { s.op = OP_OPEN; return true; }
      return false;
    }
    case PH_CONNECT: {
      uint8_t pl[7] = { (uint8_t)h, s.connIp[0], s.connIp[1], s.connIp[2], s.connIp[3],
                        (uint8_t)(s.connPort & 0xFF), (uint8_t)(s.connPort >> 8) };
      // The ESP's client.connect() is BLOCKING (up to ~5s), so it does not reply
      // until the connect resolves. Use a timeout longer than that so we don't
      // fire early and desync against the still-blocked ESP with a same-seq retry.
      if (t->espIssue(CMD_SOCK_CONNECT, pl, 7, 8000, (uint8_t)h)) { s.op = OP_CONNECT; return true; }
      return false;
    }
    case PH_LISTEN: {
      uint8_t lp[3] = { (uint8_t)h, (uint8_t)(s.lport & 0xFF), (uint8_t)(s.lport >> 8) };
      if (t->espIssue(CMD_SOCK_LISTEN, lp, 3, 200, (uint8_t)h)) { s.op = OP_LISTEN; return true; }
      return false;
    }
    case PH_CLOSE: {
      uint8_t pl[1] = { (uint8_t)h };
      if (t->espIssue(CMD_SOCK_CLOSE, pl, 1, 200, (uint8_t)h)) { s.op = OP_CLOSE; return true; }
      return false;
    }
    case PH_READY: {
      if (s.txLen > 0) {
        static uint8_t sbuf[2 + 6 + UNESP_TXBUF];
        uint16_t o = 0;
        sbuf[o++] = (uint8_t)h;
        if (s.txHasDest) {
          sbuf[o++] = AIIE_SEND_HAS_DEST;
          memcpy(sbuf + o, s.txDestIp, 4); o += 4;
          sbuf[o++] = (uint8_t)(s.txDestPort & 0xFF);
          sbuf[o++] = (uint8_t)(s.txDestPort >> 8);
        } else {
          sbuf[o++] = 0;
        }
        memcpy(sbuf + o, s.tx, s.txLen); o += s.txLen;
        if (t->espIssue(CMD_SOCK_SEND, sbuf, o, 400, (uint8_t)h)) { s.op = OP_SEND; return true; }
        return false;
      }
      if (s.rxLen == 0) {   // buffer empty: poll (also refreshes sr for connect/close)
        // Pace idle polls so we don't flood the half-duplex link. onCommandDone
        // sets nextPollMs from the outcome (immediate re-poll on data, backed-off
        // interval when empty), so an active receive streams while a quiet socket
        // is throttled.
        if ((int32_t)(t->nowMs() - s.nextPollMs) < 0) return false;
        // Cap each poll's reply well under a full MTU. Smaller frames expose fewer
        // bytes to UART corruption, so on the noisy ESP-01 link far fewer polls need
        // a retry -- and a poll that exhausts its retries loses data (a TCP-stream
        // gap that stalls the Apple), so keeping frames small is worth the extra
        // round-trips. Fits in the slot rx buffer (UNESP_RXBUF) with room to spare.
        uint16_t maxlen = UNESP_POLL_CHUNK;
        uint8_t pl[3] = { (uint8_t)h, (uint8_t)(maxlen & 0xFF), (uint8_t)(maxlen >> 8) };
        if (t->espIssue(CMD_SOCK_POLL, pl, 3, 200, (uint8_t)h)) { s.op = OP_POLL; return true; }
        return false;
      }
      return false;   // RX buffer full, nothing to send: wait for the Apple to drain
    }
  }
  return false;
}

void UnBackendEsp::service() {
  if (t->espBusy()) return;   // half-duplex: one command outstanding at a time
  for (int k = 0; k < UNESP_SLOTS; k++) {
    int h = (schedCursor + k) % UNESP_SLOTS;
    if (!slots[h].used) continue;
    if (issueFor(h)) { schedCursor = (h + 1) % UNESP_SLOTS; return; }
  }
}

void UnBackendEsp::onCommandDone(uint8_t h, bool ok, uint8_t rType,
                                 const uint8_t *rBuf, uint16_t rLen) {
  if (h >= UNESP_SLOTS || !slots[h].used) return;
  Slot &s = slots[h];
  uint8_t op = s.op;
  s.op = OP_NONE;

  if (!ok) {
    // Gave up after retries. A connect that never answered is treated as up-in-
    // doubt: go READY and let polls read the real status (avoids spinning in
    // PH_CONNECT re-issuing connects at a blocked ESP). Open can't proceed.
    if (op == OP_CONNECT) s.phase = PH_READY;
    else if (op == OP_OPEN) freeSlot(h);
    return;   // poll/send/listen/close just retry next round
  }

  switch (op) {
    case OP_OPEN:
      if (rType == EVT_SOCK_STATE && rLen >= 2) s.sr = rBuf[1];
      if (s.role == ROLE_LISTEN)      s.phase = PH_LISTEN;
      else if (s.wantConnect)         s.phase = PH_CONNECT;
      else                            s.phase = PH_READY;
      break;

    case OP_CONNECT:
      if (rType == EVT_SOCK_STATE && rLen >= 2) {
        s.sr = rBuf[1];
        if (s.sr == W5100_SR_CLOSED) s.connectFailed = true;
      }
      s.phase = PH_READY;   // SYNSENT resolves to ESTABLISHED via subsequent polls
      break;

    case OP_LISTEN:
      if (rType == EVT_SOCK_STATE && rLen >= 2) s.sr = rBuf[1];   // LISTEN
      s.phase = PH_READY;   // polls now detect an incoming connection (sr->ESTABLISHED)
      break;

    case OP_POLL: {
      bool gotData = false;
      if (rType == EVT_SOCK_DATA && rLen >= 5) {
        s.sr = rBuf[1];
        uint8_t flags = rBuf[4];
        uint16_t o = 5;
        if ((flags & AIIE_DATA_HAS_SRC) && rLen >= o + 6) {
          memcpy(s.rxSrcIp, rBuf + o, 4);
          s.rxSrcPort = (uint16_t)rBuf[o + 4] | ((uint16_t)rBuf[o + 5] << 8);
          s.rxHasSrc = true;
          o += 6;
        }
        uint16_t dlen = (rLen > o) ? (uint16_t)(rLen - o) : 0;
        if (dlen > UNESP_RXBUF) dlen = UNESP_RXBUF;
        if (dlen) { memcpy(s.rx, rBuf + o, dlen); s.rxLen = dlen; s.rxOff = 0; gotData = true; }
      }
      if (gotData) {
        s.pollIvMs = UNESP_POLL_MIN_MS;   // active: poll again as soon as the Apple drains
        s.nextPollMs = t->nowMs();
      } else {
        uint32_t iv = (uint32_t)s.pollIvMs * 2;   // idle: back off up to the max
        if (iv > UNESP_POLL_MAX_MS) iv = UNESP_POLL_MAX_MS;
        s.pollIvMs = (uint16_t)iv;
        s.nextPollMs = t->nowMs() + iv;
      }
      break;
    }

    case OP_SEND:
      if (rType == EVT_SOCK_SENT && rLen >= 3) {
        uint16_t sent = (uint16_t)rBuf[1] | ((uint16_t)rBuf[2] << 8);
        if (sent >= s.txLen) {
          s.txLen = 0;
        } else if (sent > 0) {
          memmove(s.tx, s.tx + sent, s.txLen - sent);
          s.txLen -= sent;
        }
        // sent == 0: leave staged, retried next round (ESP TX buffer momentarily full)
      }
      break;

    case OP_CLOSE:
      if (s.role == ROLE_LISTEN_CONN) {
        // The forwarded connection closed: re-open + re-LISTEN on the same port so
        // the next client can reach the Apple server (one connection at a time).
        uint16_t port = s.lport;
        freeSlot(h);
        s.used = true; s.proto = AIIE_PROTO_TCP;
        s.role = ROLE_LISTEN; s.phase = PH_OPEN; s.lport = port;
      } else {
        freeSlot(h);
      }
      break;

    default: break;
  }
}
