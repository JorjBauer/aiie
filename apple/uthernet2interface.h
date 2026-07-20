#ifndef __UTHERNET2INTERFACE_H
#define __UTHERNET2INTERFACE_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#endif

/* Uthernet2Interface is the platform-independent contract between the
 * emulated W5100 card (apple/uthernet2.cpp) and the actual network transport.
 * The card handles the Apple-facing register/buffer model; a concrete
 * subclass of this interface carries the traffic on a given platform:
 *
 *   SDL / desktop : host TCP/IP (see sdl/sdl-uthernet2.cpp)
 *   Teensy        : an ESP8266 co-processor over a UART (see
 *                   teensy/teensy-uthernet2.cpp)
 *
 * The method set mirrors the W5100's hardware socket modes so the card can
 * translate socket commands without knowing which transport is underneath.
 */

/* socket protocol, matching the low nibble of the W5100 Sn_MR register */
enum {
  U2_PROTO_TCP    = 0,
  U2_PROTO_UDP    = 1,
  U2_PROTO_IPRAW  = 2,
  U2_PROTO_MACRAW = 3,
};

/* W5100 socket-status (Sn_SR) codes, returned by socketStatus() and stored
 * verbatim into the emulated status register */
enum {
  U2_SR_CLOSED      = 0x00,
  U2_SR_INIT        = 0x13,
  U2_SR_LISTEN      = 0x14,
  U2_SR_SYNSENT     = 0x15,
  U2_SR_ESTABLISHED = 0x17,
  U2_SR_CLOSE_WAIT  = 0x1C,
  U2_SR_UDP         = 0x22,
  U2_SR_IPRAW       = 0x32,
  U2_SR_MACRAW      = 0x42,
};

#define U2_NUM_SOCKETS 4

class Uthernet2Interface {
 public:
  virtual ~Uthernet2Interface() {}

  /* One-time bring-up (open the transport, etc.). */
  virtual void begin() {}

  /* Soft reset: drop every socket and return to a clean state. */
  virtual void reset() {}

  /* True once the transport can actually carry traffic. Wired backends can
   * leave this at the default; the ESP backend reports link/association. */
  virtual bool linkReady() { return true; }

  /* Per-socket operations. sock is 0..U2_NUM_SOCKETS-1. proto is a U2_PROTO_*
   * value. ipproto is the IP protocol number, used only for U2_PROTO_IPRAW.
   * localPort is used for UDP and for a TCP server. */
  virtual void socketOpen(uint8_t sock, uint8_t proto, uint8_t ipproto,
                          uint16_t localPort) = 0;
  virtual void socketConnect(uint8_t sock, const uint8_t ip[4],
                             uint16_t port) = 0;
  virtual void socketListen(uint8_t sock, uint16_t localPort) = 0;
  virtual void socketClose(uint8_t sock) = 0;

  /* Current W5100 status (a U2_SR_* code) for this socket. */
  virtual uint8_t socketStatus(uint8_t sock) = 0;

  /* Queue data for transmission. For UDP and IP raw, destIp/destPort name the
   * peer; for TCP they are ignored. Returns the number of bytes accepted. */
  virtual int socketSend(uint8_t sock, const uint8_t *data, uint16_t len,
                         const uint8_t destIp[4], uint16_t destPort) = 0;

  /* Drain up to maxLen received bytes into buf. For UDP and IP raw,
   * srcIp/srcPort are filled with the datagram origin. Returns the byte count,
   * 0 if nothing is waiting. */
  virtual int socketRecv(uint8_t sock, uint8_t *buf, uint16_t maxLen,
                         uint8_t srcIp[4], uint16_t *srcPort) = 0;

  /* MAC-RAW transport. In MAC-RAW mode the Apple runs its own TCP/IP stack and
   * moves whole Ethernet frames; there are no per-peer sockets. sendRawFrame
   * hands one outbound frame to the backend; recvRawFrame drains one waiting
   * inbound frame into buf (returns its length, 0 if none). Backends that do
   * not support MAC-RAW leave these as no-ops. */
  virtual int sendRawFrame(const uint8_t *frame, uint16_t len) { return 0; }
  virtual int recvRawFrame(uint8_t *buf, uint16_t maxLen) { return 0; }

  /* WiFi control for backends with a real radio (the Teensy's ESP). The BIOS
   * uses these to configure and show connection status. Backends with no radio
   * (SDL's virtual network) report "up" and ignore the join. */
  virtual void wifiJoin(const char *ssid, const char *pass) { (void)ssid; (void)pass; }
  // 0 = co-processor link down, 1 = link up but WiFi not joined, 2 = connected.
  // A backend with no radio (SDL's virtual network) is always "connected".
  virtual int  wifiStatus(uint8_t ip[4]) {
    if (ip) { ip[0] = ip[1] = ip[2] = ip[3] = 0; }
    return 2;
  }

  /* Resolve a hostname to an IPv4 address. Returns true on success. */
  virtual bool resolveName(const char *host, uint8_t ip[4]) { return false; }

  /* Service asynchronous I/O; the card calls this from its maintenance tick. */
  virtual void tick(int64_t cycleCount) {}

  /* Re-read the BIOS inbound-forward settings and apply them to the live NAT,
   * so a change to the forward list takes effect without restarting the VM.
   * Backends with no host->guest forwarding leave this a no-op. */
  virtual void applyForwardConfig() {}

  /* Link health counters, for measuring the transport rather than guessing at
   * it. Backends with no wire link (e.g. host sockets) leave these at 0. */
  virtual uint32_t statFramesSent()     { return 0; } // commands sent (incl. retries)
  virtual uint32_t statFramesReceived() { return 0; } // valid frames received
  virtual uint32_t statCrcErrors()      { return 0; } // frames dropped on bad CRC
  virtual uint32_t statRetries()        { return 0; } // command resends
  virtual uint32_t statTimeouts()       { return 0; } // commands that gave up
};

#endif
