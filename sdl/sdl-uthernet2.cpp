#include "sdl-uthernet2.h"
#include "globals.h"   // g_natFwd, g_natPortOffset

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void setNonBlocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// ---- built-in DHCP responder --------------------------------------------
// Answers the Apple's DHCP request locally with a synthetic lease, so software
// that insists on DHCP (e.g. PLASMA's inet stack) can configure and run. The
// leased addresses are cosmetic here: the backend ignores the Apple's IP and
// routes real traffic through the host stack. DNS is a real resolver so name
// resolution still works. Model follows QEMU user-mode networking.
static const uint8_t DHCP_CLIENT_IP[4] = {10, 0, 2, 15};
static const uint8_t DHCP_SERVER_IP[4] = {10, 0, 2, 2}; // server id + gateway
static const uint8_t DHCP_MASK[4]      = {255, 255, 255, 0};
static const uint8_t DHCP_DNS[4]       = {8, 8, 8, 8};

// Find the DHCP message-type option (53). Returns the type, or -1.
static int dhcpMsgType(const uint8_t *p, int len)
{
  int i = 240; // past the BOOTP header (236) + magic cookie (4)
  while (i < len) {
    uint8_t t = p[i++];
    if (t == 0) continue;   // pad
    if (t == 255) break;    // end
    if (i >= len) break;
    uint8_t l = p[i++];
    if (t == 53 && l >= 1 && i < len) return p[i];
    i += l;
  }
  return -1;
}

// Build a DHCP OFFER (for DISCOVER) or ACK (for REQUEST) into out (>=300 bytes).
// Returns the reply length, or 0 if the request is not one we answer.
static int dhcpBuildReply(const uint8_t *req, int reqlen, uint8_t *out)
{
  if (reqlen < 240) return 0;
  if (!(req[236] == 0x63 && req[237] == 0x82 &&
        req[238] == 0x53 && req[239] == 0x63)) return 0; // magic cookie

  int mtype = dhcpMsgType(req, reqlen);
  uint8_t reply;
  if (mtype == 1)      reply = 2; // DISCOVER -> OFFER
  else if (mtype == 3) reply = 5; // REQUEST  -> ACK
  else return 0;

  memset(out, 0, 300);
  out[0] = 2;                    // op = BOOTREPLY
  out[1] = req[1];               // htype
  out[2] = req[2];               // hlen
  memcpy(out + 4, req + 4, 4);   // xid
  out[10] = req[10]; out[11] = req[11];   // flags
  memcpy(out + 16, DHCP_CLIENT_IP, 4);    // yiaddr
  memcpy(out + 20, DHCP_SERVER_IP, 4);    // siaddr
  memcpy(out + 24, req + 24, 4);          // giaddr (echo)
  memcpy(out + 28, req + 28, 16);         // chaddr (echo client MAC)
  out[236] = 0x63; out[237] = 0x82; out[238] = 0x53; out[239] = 0x63;

  int o = 240;
  out[o++] = 53; out[o++] = 1; out[o++] = reply;                       // msg type
  out[o++] = 54; out[o++] = 4; memcpy(out + o, DHCP_SERVER_IP, 4); o += 4; // server id
  out[o++] = 51; out[o++] = 4; out[o++] = 0; out[o++] = 1; out[o++] = 0x51; out[o++] = 0x80; // lease 86400s
  out[o++] = 1;  out[o++] = 4; memcpy(out + o, DHCP_MASK, 4);      o += 4; // subnet
  out[o++] = 3;  out[o++] = 4; memcpy(out + o, DHCP_SERVER_IP, 4); o += 4; // router
  out[o++] = 6;  out[o++] = 4; memcpy(out + o, DHCP_DNS, 4);       o += 4; // DNS
  out[o++] = 255;                                                          // end
  return o;
}

// The host-side inbound port offset: BIOS setting g_natPortOffset (default 8000),
// with AIIE_UTHERNET_PORT_OFFSET as an override for scripted runs.
static uint16_t natOffset()
{
  const char *env = getenv("AIIE_UTHERNET_PORT_OFFSET");
  if (env) {
    long v = strtol(env, NULL, 10);
    if (v >= 0 && v <= 65535) return (uint16_t)v;
  }
  return g_natPortOffset;
}

// Build the UserNet "hostport:appleport,..." forward string from the BIOS setting
// g_natFwd (a bare list of Apple ports, e.g. "6580,23"), applying the offset to
// privileged Apple ports. AIIE_USERNET_HOSTFWD (full host:apple spec) overrides.
static const char *natHostfwd()
{
  const char *env = getenv("AIIE_USERNET_HOSTFWD");
  if (env) return env;
  if (!g_natFwd[0]) return NULL;

  static char buf[160];
  uint16_t off = natOffset();
  int o = 0;
  buf[0] = 0;
  const char *p = g_natFwd;
  while (*p) {
    while (*p == ' ' || *p == ',') p++;
    int ap = 0; bool got = false;
    while (*p >= '0' && *p <= '9') { ap = ap * 10 + (*p++ - '0'); got = true; }
    if (got && ap > 0 && ap < 65536) {
      int hp = (ap < 1024 && off) ? (ap + off) : ap;
      if (hp > 65535) hp = ap;
      o += snprintf(buf + o, sizeof(buf) - o, "%s%d:%d", o ? "," : "", hp, ap);
    }
    while (*p && *p != ',' && !(*p >= '0' && *p <= '9')) p++; // skip separators/junk
  }
  return buf[0] ? buf : NULL;
}

SDLUthernet2::SDLUthernet2()
  : usernet(&unBsd, getenv("AIIE_USERNET_DEBUG") != NULL, natHostfwd())
{
  for (int i = 0; i < U2_NUM_SOCKETS; i++) {
    fd[i] = -1;
    status[i] = U2_SR_CLOSED;
    proto[i] = 0xFF;
    localPort[i] = 0;
    boundHostPort[i] = 0;
    boundApplePort[i] = 0;
    pendLen[i] = 0;
  }
  inboundOffset = natOffset();
}

void SDLUthernet2::applyForwardConfig()
{
  // Re-read the BIOS settings and apply them to the running NAT. Pick up any
  // change to the port offset first so the rebuilt forward string and the live
  // inbound mapping agree, then re-open the host listeners.
  inboundOffset = natOffset();
  usernet.reconfigureForwards(natHostfwd());
}

uint16_t SDLUthernet2::mapInboundPort(uint16_t applePort) const
{
  // Only privileged ports need remapping; unprivileged ports bind as-is so the
  // host connects to the same number the Apple used.
  if (applePort == 0 || applePort >= 1024 || inboundOffset == 0) return applePort;
  uint32_t mapped = (uint32_t)applePort + inboundOffset;
  return (mapped > 65535) ? applePort : (uint16_t)mapped;
}

SDLUthernet2::~SDLUthernet2()
{
  reset();
}

void SDLUthernet2::begin()
{
}

void SDLUthernet2::reset()
{
  for (int i = 0; i < U2_NUM_SOCKETS; i++) {
    socketClose(i);
  }
}

void SDLUthernet2::socketOpen(uint8_t sock, uint8_t p, uint8_t ipproto,
                              uint16_t lport)
{
  (void)ipproto;
  if (sock >= U2_NUM_SOCKETS) return;
  socketClose(sock);
  proto[sock] = p;
  localPort[sock] = lport;

  if (p == U2_PROTO_TCP) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return;
    setNonBlocking(s);
    fd[sock] = s;
    status[sock] = U2_SR_INIT;
  } else if (p == U2_PROTO_UDP) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) return;
    setNonBlocking(s);
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    // A UDP server (e.g. a TFTP server on port 69) needs a reachable host bind,
    // so a privileged Apple port goes through the same inbound-NAT offset the
    // TCP listen path uses. An unprivileged port binds as-is. DHCP is unaffected
    // since its replies are synthesized locally rather than via this socket.
    uint16_t hostPort = mapInboundPort(lport);
    a.sin_port = htons(hostPort);
    if (bind(s, (struct sockaddr *)&a, sizeof(a)) < 0 && hostPort != lport) {
      fprintf(stderr, "Uthernet: UDP socket %d cannot bind host port %u "
              "(Apple port %u): %s\n", sock, hostPort, lport, strerror(errno));
    }
    fd[sock] = s;
    status[sock] = U2_SR_UDP;
  } else if (p == U2_PROTO_MACRAW) {
    // Own-stack software: the built-in user-mode network stands in for a LAN.
    usernet.reset();
    status[sock] = U2_SR_MACRAW;
  }
  // IP raw is not supported on this back end; leave CLOSED.
}

int SDLUthernet2::sendRawFrame(const uint8_t *frame, uint16_t len)
{
  usernet.fromApple(frame, len);
  return len;
}

int SDLUthernet2::recvRawFrame(uint8_t *buf, uint16_t maxLen)
{
  return (int)usernet.toApple(buf, maxLen);
}

void SDLUthernet2::socketConnect(uint8_t sock, const uint8_t ip[4],
                                 uint16_t port)
{
  if (sock >= U2_NUM_SOCKETS || fd[sock] < 0) return;
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  memcpy(&a.sin_addr.s_addr, ip, 4); // already network order
  a.sin_port = htons(port);

  int r = connect(fd[sock], (struct sockaddr *)&a, sizeof(a));
  if (r == 0) {
    status[sock] = U2_SR_ESTABLISHED;
  } else if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
    status[sock] = U2_SR_SYNSENT;
  } else {
    socketClose(sock);
  }
}

void SDLUthernet2::socketListen(uint8_t sock, uint16_t lport)
{
  if (sock >= U2_NUM_SOCKETS || fd[sock] < 0) return;
  // LISTEN is idempotent. Apple software polls by re-issuing LISTEN in a loop,
  // so once the socket is already listening (or has since connected) this must
  // be a no-op; re-binding an already-bound socket would fail.
  if (status[sock] == U2_SR_LISTEN || status[sock] == U2_SR_ESTABLISHED) return;
  uint16_t applePort = lport ? lport : localPort[sock];
  // Keep the host port stable across a server's close/re-listen cycle: reuse
  // the port we bound last time for this Apple port, so clients keep the same
  // address between requests even if it started as an ephemeral fallback.
  uint16_t hostPort = (boundHostPort[sock] && boundApplePort[sock] == applePort)
                        ? boundHostPort[sock]
                        : mapInboundPort(applePort);

  int yes = 1;
  setsockopt(fd[sock], SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = INADDR_ANY;
  a.sin_port = htons(hostPort);
  if (bind(fd[sock], (struct sockaddr *)&a, sizeof(a)) < 0) {
    // Mapped port unavailable (in use, or still privileged): fall back to an
    // OS-assigned ephemeral port so the server still comes up. The log below
    // reports the actual port to connect to.
    const int firstErr = errno;
    a.sin_port = htons(0);
    if (bind(fd[sock], (struct sockaddr *)&a, sizeof(a)) < 0) {
      fprintf(stderr, "Uthernet: socket %d cannot bind (Apple port %u): %s\n",
              sock, applePort, strerror(firstErr));
      socketClose(sock);
      return;
    }
  }
  if (listen(fd[sock], 1) < 0) { socketClose(sock); return; }
  status[sock] = U2_SR_LISTEN;

  // Report the actual bound port (the mapped one, or the ephemeral fallback).
  struct sockaddr_in bound;
  socklen_t blen = sizeof(bound);
  uint16_t actual = hostPort;
  if (getsockname(fd[sock], (struct sockaddr *)&bound, &blen) == 0)
    actual = ntohs(bound.sin_port);
  boundHostPort[sock] = actual;      // remember for a stable re-listen
  boundApplePort[sock] = applePort;
  if (actual == applePort)
    fprintf(stderr, "Uthernet: socket %d listening on localhost:%u\n", sock, actual);
  else
    fprintf(stderr, "Uthernet: socket %d listening on localhost:%u (Apple port %u, inbound NAT)\n",
            sock, actual, applePort);
}

void SDLUthernet2::socketClose(uint8_t sock)
{
  if (sock >= U2_NUM_SOCKETS) return;
  if (fd[sock] >= 0) {
    close(fd[sock]);
    fd[sock] = -1;
  }
  status[sock] = U2_SR_CLOSED;
  proto[sock] = 0xFF;
}

void SDLUthernet2::serviceSocket(uint8_t sock)
{
  if (fd[sock] < 0) return;

  if (status[sock] == U2_SR_SYNSENT) {
    struct pollfd pfd;
    pfd.fd = fd[sock];
    pfd.events = POLLOUT;
    if (poll(&pfd, 1, 0) > 0) {
      int err = 0;
      socklen_t elen = sizeof(err);
      if (getsockopt(fd[sock], SOL_SOCKET, SO_ERROR, &err, &elen) == 0 && err == 0) {
        status[sock] = U2_SR_ESTABLISHED;
      } else {
        socketClose(sock);
      }
    }
  } else if (status[sock] == U2_SR_LISTEN) {
    int c = accept(fd[sock], NULL, NULL);
    if (c >= 0) {
      close(fd[sock]); // stop listening; the socket becomes the connection
      setNonBlocking(c);
      fd[sock] = c;
      status[sock] = U2_SR_ESTABLISHED;
    }
  } else if (status[sock] == U2_SR_ESTABLISHED && proto[sock] == U2_PROTO_TCP) {
    // Detect the peer closing so a status poll reports CLOSE_WAIT, as the
    // W5100 does. Without this a server never learns the client disconnected
    // and cannot close and re-listen. MSG_PEEK does not consume any data.
    char probe;
    ssize_t n = recv(fd[sock], &probe, 1, MSG_PEEK);
    if (n == 0) {
      status[sock] = U2_SR_CLOSE_WAIT;   // peer sent FIN, nothing left to read
    } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
      status[sock] = U2_SR_CLOSE_WAIT;
    }
    // n > 0: unread data pending; stay ESTABLISHED (the card pulls it via RX).
  }
}

uint8_t SDLUthernet2::socketStatus(uint8_t sock)
{
  if (sock >= U2_NUM_SOCKETS) return U2_SR_CLOSED;
  serviceSocket(sock);
  return status[sock];
}

int SDLUthernet2::socketSend(uint8_t sock, const uint8_t *data, uint16_t len,
                             const uint8_t destIp[4], uint16_t destPort)
{
  if (sock >= U2_NUM_SOCKETS || fd[sock] < 0) return 0;

  if (proto[sock] == U2_PROTO_TCP) {
    if (status[sock] != U2_SR_ESTABLISHED) return 0;
    ssize_t n = send(fd[sock], data, len, 0);
    if (n < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
      socketClose(sock);
      return 0;
    }
    return (int)n;
  } else if (proto[sock] == U2_PROTO_UDP) {
    // Answer DHCP (UDP port 67) locally instead of putting it on the wire.
    if (destPort == 67) {
      int rl = dhcpBuildReply(data, len, pendData[sock]);
      if (rl > 0) {
        pendLen[sock] = (uint16_t)rl;
        memcpy(pendSrcIp[sock], DHCP_SERVER_IP, 4);
        pendSrcPort[sock] = 67;
      }
      return len; // report the whole datagram as sent
    }
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    memcpy(&a.sin_addr.s_addr, destIp, 4);
    a.sin_port = htons(destPort);
    ssize_t n = sendto(fd[sock], data, len, 0, (struct sockaddr *)&a, sizeof(a));
    if (n < 0) return 0;
    return (int)n;
  }
  return 0;
}

int SDLUthernet2::socketRecv(uint8_t sock, uint8_t *buf, uint16_t maxLen,
                             uint8_t srcIp[4], uint16_t *srcPort)
{
  if (sock >= U2_NUM_SOCKETS || fd[sock] < 0 || maxLen == 0) return 0;

  // Deliver a locally-generated datagram (e.g. a DHCP reply) first.
  if (pendLen[sock] > 0) {
    uint16_t n = (pendLen[sock] > maxLen) ? maxLen : pendLen[sock];
    memcpy(buf, pendData[sock], n);
    if (srcIp) memcpy(srcIp, pendSrcIp[sock], 4);
    if (srcPort) *srcPort = pendSrcPort[sock];
    pendLen[sock] = 0;
    return (int)n;
  }

  if (proto[sock] == U2_PROTO_TCP) {
    ssize_t n = recv(fd[sock], buf, maxLen, 0);
    if (n == 0) {
      // peer closed; the host will see CLOSE_WAIT and issue CLOSE
      status[sock] = U2_SR_CLOSE_WAIT;
      return 0;
    }
    if (n < 0) return 0; // EAGAIN etc.
    return (int)n;
  } else if (proto[sock] == U2_PROTO_UDP) {
    struct sockaddr_in a;
    socklen_t alen = sizeof(a);
    memset(&a, 0, sizeof(a));
    ssize_t n = recvfrom(fd[sock], buf, maxLen, 0, (struct sockaddr *)&a, &alen);
    if (n <= 0) return 0;
    if (srcIp) memcpy(srcIp, &a.sin_addr.s_addr, 4);
    if (srcPort) *srcPort = ntohs(a.sin_port);
    return (int)n;
  }
  return 0;
}

bool SDLUthernet2::resolveName(const char *host, uint8_t ip[4])
{
  struct addrinfo hints;
  struct addrinfo *res = NULL;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) return false;

  struct sockaddr_in *a = (struct sockaddr_in *)res->ai_addr;
  memcpy(ip, &a->sin_addr.s_addr, 4);
  freeaddrinfo(res);
  return true;
}

void SDLUthernet2::tick(int64_t cycleCount)
{
  (void)cycleCount;
  for (int i = 0; i < U2_NUM_SOCKETS; i++) {
    serviceSocket(i);
  }
  usernet.tick();
}
