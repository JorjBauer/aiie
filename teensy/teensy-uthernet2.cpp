#include "teensy-uthernet2.h"
#include "protocol.h"
#include "globals.h"
#include <string.h>
#include <stdio.h>

TeensyUthernet2 *TeensyUthernet2::s_instance = nullptr;

// Build the "hostport:appleport,..." forward string from the BIOS setting
// g_natFwd (a bare Apple-port list, e.g. "80,23"). The Teensy forwards through
// the ESP's WiFi stack, so the host port equals the Apple port -- no privileged-
// port offset is needed. Returns `fallback` if the BIOS list is empty.
static const char *teensyBuildFwd(const char *fallback)
{
  if (!g_natFwd[0]) return fallback;
  static char buf[160];
  int o = 0; buf[0] = 0;
  const char *p = g_natFwd;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    int ap = 0; bool got = false;
    while (*p >= '0' && *p <= '9') { ap = ap * 10 + (*p++ - '0'); got = true; }
    if (got && ap > 0 && ap < 65536)
      o += snprintf(buf + o, sizeof(buf) - o, "%s%d:%d", o ? "," : "", ap, ap);
    while (*p && *p != ',' && !(*p >= '0' && *p <= '9')) p++;
  }
  return buf[0] ? buf : fallback;
}

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
  : link(l), parser(frameCb), unEsp(this),
    usernet(&unEsp, false, teensyBuildFwd(hostfwd)),
    macraw(false)
{
  s_instance = this;
  linkUp = false;
  lastProbe = 0;
  lastServiceMs = 0;
  seq = 0;
  cTx = 0; cRetries = 0; cTimeouts = 0;
  rpGot = false; rpType = 0; rpSeq = 0; rpLen = 0;
  cmdInFlight = false; cmdDoneFlag = false; cmdOkFlag = false;
  cmdType = 0; cmdSeq = 0; cmdTries = 0;
  cmdSentMs = 0; cmdTimeoutMs = 0; cmdPayLen = 0;
  cmdOwner = 0; cmdTag = 0;
  pollCursor = 0;
  haveCreds = false;
  ssid[0] = 0; pass[0] = 0;
  for (uint8_t i = 0; i < U2_NUM_SOCKETS; i++) {
    sr[i] = U2_SR_CLOSED;
    proto[i] = 0xFF;
    rxLen[i] = 0; rxOff[i] = 0;
    rxHasSrc[i] = false; rxSrcPort[i] = 0;
    sockNextPollMs[i] = 0; sockPollIvMs[i] = TU2_SERVICE_MS;
  }
}

TeensyUthernet2::~TeensyUthernet2()
{
  if (s_instance == this) s_instance = nullptr;
}

void TeensyUthernet2::applyForwardConfig()
{
  // Re-read the BIOS forward list and apply it to the running NAT, so a change
  // takes effect without restarting the VM.
  usernet.reconfigureForwards(teensyBuildFwd(nullptr));
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

// Start a command without waiting. One seq is reused across retries: the ESP
// dedups by seq, so a resend either gets reprocessed (first never arrived) or
// re-fetches the cached reply (only the reply was lost), never double-executing.
bool TeensyUthernet2::issue(uint8_t type, const uint8_t *payload, uint16_t len,
                            uint32_t timeoutMs, uint8_t owner, uint8_t tag)
{
  if (cmdInFlight) return false;
  if (len > sizeof(cmdPayload)) return false;
  cmdType = type;
  cmdSeq = nextSeq();
  cmdPayLen = len;
  if (len && payload) memcpy(cmdPayload, payload, len);
  cmdTimeoutMs = timeoutMs;
  cmdOwner = owner;
  cmdTag = tag;
  cmdTries = 0;
  rpGot = false;
  cmdDoneFlag = false;
  cmdOkFlag = false;
  frameSend(*link, cmdType, cmdSeq, cmdPayLen ? cmdPayload : nullptr, cmdPayLen);
  cTx++;
  cmdSentMs = millis();
  cmdInFlight = true;
  return true;
}

// Advance the outstanding command without blocking: consume whatever bytes the
// UART already buffered, and if the matching reply completes, finish (rp* holds
// it). On timeout, resend up to TU2_MAX_RETRIES, then give up. Safe to call when
// nothing is in flight (no-op).
// Finish the in-flight command. If it belonged to UnBackendEsp (owner 1), hand
// the reply to it IMMEDIATELY -- even when called from a blocking command()'s
// pump loop -- so its socket data is never clobbered by the next issue().
void TeensyUthernet2::completeCommand(bool ok)
{
  cmdInFlight = false;
  cmdDoneFlag = true;
  cmdOkFlag = ok;
  if (cmdOwner == 1) {
    cmdOwner = 0;
    unEsp.onCommandDone(cmdTag, ok, rpType, rpBuf, rpLen);
  }
}

void TeensyUthernet2::pump()
{
  if (!cmdInFlight) return;

  while (link->available()) {
    parser.feed((uint8_t)link->read());
    if (rpGot && rpSeq == cmdSeq) {   // our reply, captured in rp* by onFrame
      completeCommand(true);
      return;
    }
    rpGot = false;                    // unmatched frame; keep hunting
  }

  if ((uint32_t)(millis() - cmdSentMs) >= cmdTimeoutMs) {
    if (cmdTries < TU2_MAX_RETRIES) {
      cmdTries++;
      cRetries++;
      rpGot = false;
      frameSend(*link, cmdType, cmdSeq, cmdPayLen ? cmdPayload : nullptr, cmdPayLen);
      cTx++;
      cmdSentMs = millis();
    } else {
      cTimeouts++;
      completeCommand(false);
    }
  }
}

// EspTransport async surface: start a command owned by UnBackendEsp.
bool TeensyUthernet2::espIssue(uint8_t type, const uint8_t *payload, uint16_t len,
                               uint32_t timeoutMs, uint8_t tag)
{
  return issue(type, payload, len, timeoutMs, /*owner=*/1, tag);
}

// Blocking wrapper for the control path (boot ping, WiFi join, DNS): wait for any
// in-flight async command, issue this one, then pump until it completes. Same
// external behavior as before; now built on the async engine.
bool TeensyUthernet2::command(uint8_t type, const uint8_t *payload, uint16_t len,
                              uint32_t timeoutMs)
{
  while (cmdInFlight) { pump(); yield(); }
  if (!issue(type, payload, len, timeoutMs)) return false;
  while (!cmdDoneFlag) { pump(); yield(); }
  return cmdOkFlag;
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
uint32_t TeensyUthernet2::nowMs()   { return millis(); }

int TeensyUthernet2::sendRawFrame(const uint8_t *frame, uint16_t len)
{
  usernet.fromApple(frame, len);
  return len;
}

int TeensyUthernet2::recvRawFrame(uint8_t *buf, uint16_t maxLen)
{
  return (int)usernet.toApple(buf, maxLen);
}

void TeensyUthernet2::wifiJoin(const char *ssid, const char *pass)
{
  setNetwork(ssid, pass);
  begin();  // re-probe the link and (re)join with the new credentials
}

bool TeensyUthernet2::pingOnce()
{
  // One link probe. The ESP is reply-only, so we ping and see if it answers.
  uint8_t echo = 0x5A;
  if (command(CMD_LINK_PING, &echo, 1, 60)) linkUp = true;
  return linkUp;
}

bool TeensyUthernet2::probeLink()
{
  // Throttled re-probe for repeated polling (tick and the BIOS status screen):
  // at most one ping every 3s while down, so a late-booting or absent ESP is
  // detected without blocking the caller on every call.
  if (linkUp) return true;
  if (lastProbe && (uint32_t)(millis() - lastProbe) < 3000) return false;
  lastProbe = millis();
  return pingOnce();
}

int TeensyUthernet2::wifiStatus(uint8_t ip[4])
{
  if (ip) { ip[0] = ip[1] = ip[2] = ip[3] = 0; }
  // Re-probe when down so the BIOS WiFi screen is a live link test: an ESP that
  // was not ready when the card was enabled can still come up and be detected.
  if (!linkUp) probeLink();
  if (!linkUp) return 0;   // the ESP co-processor is not answering
  if (command(CMD_WIFI_STATUS, nullptr, 0, 300) && rpType == EVT_WIFI && rpLen >= 5) {
    if (ip) { ip[0] = rpBuf[1]; ip[1] = rpBuf[2]; ip[2] = rpBuf[3]; ip[3] = rpBuf[4]; }
    return rpBuf[0] ? 2 : 1;   // 2 = joined (IP valid), 1 = link up, not joined
  }
  return 1;   // link is up but the status query failed
}

uint32_t TeensyUthernet2::statFramesSent()     { return cTx; }
uint32_t TeensyUthernet2::statFramesReceived() { return parser.framesOk; }
uint32_t TeensyUthernet2::statCrcErrors()      { return parser.crcErrors; }
uint32_t TeensyUthernet2::statRetries()        { return cRetries; }
uint32_t TeensyUthernet2::statTimeouts()       { return cTimeouts; }

void TeensyUthernet2::debugNetState(char *buf, uint16_t n)
{
  // One persistent line for the MAC-RAW NAT (the transfer fails too fast to read
  // live flow state). All fields survive the flow closing:
  //   RX  = reply frames from the ESP (liveness)     TO = command timeouts (lost data)
  //   tH  = request bytes forwarded to the host       rA = response bytes to the Apple
  //   ls  = last flow state at close (3 EST 4 FIN)     lw/li = its Apple-window / unACKed
  // rA stalling while li<lw => the window was open but the backend stopped feeding
  // data; li>=lw => the Apple's window shut (it stopped ACKing). TO climbing => the
  // link is losing command replies.
  uint32_t tH = 0, rA = 0; usernet.debugBytes(tH, rA);
  uint8_t ls = 0; uint32_t lw = 0, li = 0; usernet.debugLastClose(ls, lw, li);
  snprintf(buf, n, "RX%lu tH%lu rA%lu ls%u lw%lu li%lu TO%lu   ",
           (unsigned long)statFramesReceived(), (unsigned long)tH, (unsigned long)rA,
           ls, (unsigned long)lw, (unsigned long)li, (unsigned long)statTimeouts());
}

void TeensyUthernet2::begin()
{
  linkUp = false;
  // Aggressive burst at startup so a ready ESP is found quickly; later re-probes
  // (tick, wifiStatus) are throttled via probeLink().
  for (int i = 0; i < 10 && !linkUp; i++) pingOnce();
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
    sockNextPollMs[i] = 0; sockPollIvMs[i] = TU2_SERVICE_MS;
  }
  macraw = false;
  usernet.reset();
  unEsp.reset();
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

  sockNextPollMs[sock] = 0; sockPollIvMs[sock] = TU2_SERVICE_MS; // poll promptly
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
  sockNextPollMs[sock] = 0; sockPollIvMs[sock] = TU2_SERVICE_MS; // data coming: poll fast
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
    probeLink();  // throttled retry so the link recovers if the ESP came up late
    return;
  }

  // MAC-RAW mode: fully async. pump() advances the outstanding ESP command
  // without blocking (it consumes only bytes the UART already buffered); a long
  // reply is assembled over many tick() passes while the 6502 keeps running.
  // service() issues the next slot's command when the engine is idle, and
  // usernet.tick() runs the NAT state machine against the now-non-blocking
  // backend. No real-time throttle needed -- nothing here stalls the emulator.
  //
  // This does NOT return afterward: an app can run its own IP stack over MAC-RAW
  // (ARP/DNS/ICMP) while ALSO opening W5100 hardware sockets for TCP. Those
  // hardware sockets still need servicing below, or a hardware connection
  // connects but is never polled for received data and hangs forever on
  // "receiving" (e.g. IP65's WGET: DNS over MAC-RAW, TCP over a hardware socket).
  if (macraw) {
    pump();
    unEsp.service();
    usernet.tick();
  }

  // Hardware-socket path: poll any real (non-MAC-RAW) sockets. Uses the blocking
  // command(), so pace it against real time. Runs whether or not MAC-RAW is also
  // active; the MAC-RAW socket itself is skipped (it is not a real ESP socket).
  uint32_t now = millis();
  if ((uint32_t)(now - lastServiceMs) < TU2_SERVICE_MS) return;
  lastServiceMs = now;

  // Service at most one active socket per tick (round-robin), to bound the
  // half-duplex round-trip cost per maintenance call.
  for (uint8_t k = 0; k < U2_NUM_SOCKETS; k++) {
    uint8_t s = (pollCursor + k) % U2_NUM_SOCKETS;
    if (proto[s] == 0xFF || proto[s] == U2_PROTO_MACRAW) continue;
    if (rxLen[s] != 0) continue;                            // still draining: skip
    if ((int32_t)(now - sockNextPollMs[s]) < 0) continue;   // paced: not due yet
    pollCursor = (s + 1) % U2_NUM_SOCKETS;

    uint16_t maxlen = TU2_RXBUF;
    uint8_t pl[3] = { s, (uint8_t)(maxlen & 0xFF), (uint8_t)(maxlen >> 8) };
    bool gotData = false;
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
      if (dlen) { memcpy(rxData[s], rpBuf + o, dlen); gotData = true; }
      rxLen[s] = dlen;
      rxOff[s] = 0;
    }
    // Pace the next poll. Poll at full speed whenever data is flowing OR the
    // connection is live (ESTABLISHED): the Apple polls its receive-size register
    // synchronously with a timeout, and our poll IS its data path, so adding
    // latency to a live connection makes it miss data and report the connection
    // lost. Only back off once the socket is no longer established
    // (closing/closed/idle/listening) -- which is where the unbounded post-transfer
    // poll chatter actually lives (e.g. a socket left open at a "press any key").
    if (gotData || sr[s] == U2_SR_ESTABLISHED) {
      sockPollIvMs[s] = TU2_SERVICE_MS;
      sockNextPollMs[s] = now;
    } else {
      uint32_t iv = (uint32_t)sockPollIvMs[s] * 2;
      if (iv > TU2_POLL_MAX_MS) iv = TU2_POLL_MAX_MS;
      sockPollIvMs[s] = (uint16_t)iv;
      sockNextPollMs[s] = now + iv;
    }
    return; // one socket per tick
  }
}
