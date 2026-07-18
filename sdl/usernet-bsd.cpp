#include "usernet-bsd.h"
#include <string.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static void setNonBlock(int fd) {
  int fl = fcntl(fd, F_GETFL, 0);
  if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int UnBackendBsd::tcpOpen() {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return -1;
  setNonBlock(s);
  return s;
}

bool UnBackendBsd::tcpConnect(int h, const uint8_t ip[4], uint16_t port) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  memcpy(&a.sin_addr.s_addr, ip, 4);
  a.sin_port = htons(port);
  int r = connect(h, (struct sockaddr *)&a, sizeof(a));
  // Success or in-progress are both fine; only a hard error means failure.
  return r == 0 || errno == EINPROGRESS || errno == EWOULDBLOCK;
}

int UnBackendBsd::tcpConnectPoll(int h) {
  struct pollfd pfd; pfd.fd = h; pfd.events = POLLOUT; pfd.revents = 0;
  if (poll(&pfd, 1, 0) <= 0) return 0;                 // still connecting
  if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) return -1;
  if (!(pfd.revents & POLLOUT)) return 0;
  int err = 0; socklen_t el = sizeof(err);
  if (getsockopt(h, SOL_SOCKET, SO_ERROR, &err, &el) != 0 || err != 0) return -1;
  return 1;
}

int UnBackendBsd::tcpSend(int h, const uint8_t *data, uint16_t len) {
  ssize_t n = send(h, data, len, 0);
  if (n >= 0) return (int)n;
  if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
  return -1;
}

int UnBackendBsd::tcpRecv(int h, uint8_t *buf, uint16_t maxLen) {
  ssize_t n = recv(h, buf, maxLen, 0);
  if (n > 0) return (int)n;
  if (n == 0) return -1;                                // peer closed
  if (errno == EAGAIN || errno == EWOULDBLOCK) return 0;
  return -1;
}

void UnBackendBsd::tcpShutdownWrite(int h) { shutdown(h, SHUT_WR); }

int UnBackendBsd::udpOpen(uint16_t bindPort) {
  int s = socket(AF_INET, SOCK_DGRAM, 0);
  if (s < 0) return -1;
  setNonBlock(s);
  if (bindPort) {
    int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(bindPort);
    bind(s, (struct sockaddr *)&a, sizeof(a));
  }
  return s;
}

int UnBackendBsd::udpSend(int h, const uint8_t ip[4], uint16_t port,
                          const uint8_t *data, uint16_t len) {
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  memcpy(&a.sin_addr.s_addr, ip, 4);
  a.sin_port = htons(port);
  ssize_t n = sendto(h, data, len, 0, (struct sockaddr *)&a, sizeof(a));
  return (n < 0) ? -1 : (int)n;
}

int UnBackendBsd::udpRecv(int h, uint8_t *buf, uint16_t maxLen,
                          uint8_t srcIp[4], uint16_t *srcPort) {
  struct sockaddr_in a; socklen_t al = sizeof(a);
  memset(&a, 0, sizeof(a));
  ssize_t n = recvfrom(h, buf, maxLen, 0, (struct sockaddr *)&a, &al);
  if (n <= 0) return 0;
  if (srcIp) memcpy(srcIp, &a.sin_addr.s_addr, 4); // network order
  if (srcPort) *srcPort = ntohs(a.sin_port);
  return (int)n;
}

void UnBackendBsd::sockClose(int h) { if (h >= 0) close(h); }

uint32_t UnBackendBsd::nowSecs() { return (uint32_t)time(NULL); }

int UnBackendBsd::tcpListen(uint16_t hostPort) {
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return -1;
  int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  a.sin_port = htons(hostPort);
  if (bind(s, (struct sockaddr *)&a, sizeof(a)) != 0 || listen(s, 4) != 0) {
    close(s);
    return -1;
  }
  setNonBlock(s);
  return s;
}

int UnBackendBsd::tcpAccept(int listenH) {
  int c = accept(listenH, NULL, NULL);
  if (c < 0) return -1;
  setNonBlock(c);
  return c;
}
