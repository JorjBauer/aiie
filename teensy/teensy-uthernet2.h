#ifndef __TEENSY_UTHERNET2_H
#define __TEENSY_UTHERNET2_H

#include <Arduino.h>
#include "uthernet2interface.h"
#include "frame.h"
#include "esptransport.h"
#include "usernet-esp.h"
#include "usernet.h"

/* TeensyUthernet2 carries Uthernet2 traffic to an ESP8266 co-processor over a
 * UART, using the framed binary protocol in protocol.h. The ESP runs the real
 * TCP/IP stack; this class translates the interface calls into protocol frames.
 *
 * The link is half-duplex (SoftwareSerial on the free A4/A5 pins), so the
 * protocol is strictly master/slave: every method that talks to the ESP sends
 * one command and reads exactly one reply. To keep the 6502 from stalling on a
 * round-trip, socketStatus() and socketRecv() are served from a local shadow
 * and a per-socket RX buffer that tick() refreshes by polling the ESP.
 */

// Per-socket local receive buffer. Sized to one Ethernet MTU so a whole UDP
// datagram (which must be delivered atomically) always fits; a smaller buffer
// would silently drop datagrams larger than it, e.g. a 516-byte TFTP block.
#define TU2_RXBUF 1460
// Resends before a command gives up. High on purpose: a POLL that gives up loses
// data the ESP already consumed, tearing a gap in the TCP stream that stalls the
// Apple's stack. On this async path a reply is occasionally lost -- a large frame
// can pile up in the UART RX buffer while the main loop is busy blitting the
// display, or an odd byte corrupts -- so retrying re-fetches the ESP's cached
// reply and recovers before a gap forms.
#define TU2_MAX_RETRIES 8  // resends before a command gives up (9 tries total)
// Real-time pacing for ESP servicing in tick(). cpuMaintenance() calls tick()
// roughly every 24 emulated CPU cycles, but each ESP poll/send is a blocking
// UART round-trip -- doing one per call froze the 6502 (and the BIOS button) to
// a crawl whenever a socket was open. Service the ESP at most this often (in
// real milliseconds) so the emulator runs at full speed between passes.
#define TU2_SERVICE_MS 3
// Idle-poll backoff for the hardware-socket path (mirrors the MAC-RAW path). A
// hardware socket with no data pending is polled no faster than TU2_SERVICE_MS,
// and the interval doubles on each empty poll up to this ceiling; a poll that
// returns data snaps it back to the minimum. Keeps an idle/quiet connection from
// spinning ~330 empty CMD_SOCK_POLL round-trips per second on the half-duplex
// link while an active receive still streams at full speed.
#define TU2_POLL_MAX_MS 250

class TeensyUthernet2 : public Uthernet2Interface, public EspTransport {
 public:
  // hostfwd optionally forwards ESP (WiFi) ports to Apple server ports, e.g.
  // "80:80" so a LAN client reaching the ESP's IP:80 hits the Apple's webserver.
  // Format "espPort:applePort[,...]"; NULL disables inbound forwarding.
  TeensyUthernet2(Stream *link, const char *hostfwd = nullptr);
  virtual ~TeensyUthernet2();

  // EspTransport: lets UnBackendEsp issue socket commands over this link.
  virtual bool espCommand(uint8_t type, const uint8_t *payload, uint16_t len,
                          uint8_t &rType, uint8_t *rBuf, uint16_t rCap,
                          uint16_t &rLen, uint32_t timeoutMs);
  virtual uint32_t nowSecs();
  virtual uint32_t nowMs();
  // Async engine surface for UnBackendEsp's non-blocking scheduler.
  virtual bool espBusy() { return cmdInFlight; }
  virtual bool espIssue(uint8_t type, const uint8_t *payload, uint16_t len,
                        uint32_t timeoutMs, uint8_t tag);

  // Provide the AP credentials the ESP should join in begin(). Optional.
  void setNetwork(const char *ssid, const char *pass);

  virtual void begin();
  virtual void reset();
  virtual bool linkReady();
  virtual void applyForwardConfig();

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

  // MAC-RAW (own-stack software): frames are handled by the on-Teensy UserNet,
  // which NATs to the ESP's sockets.
  virtual int sendRawFrame(const uint8_t *frame, uint16_t len);
  virtual int recvRawFrame(uint8_t *buf, uint16_t maxLen);

  virtual void wifiJoin(const char *ssid, const char *pass);
  virtual int  wifiStatus(uint8_t ip[4]);

  virtual bool resolveName(const char *host, uint8_t ip[4]);
  virtual bool espInfo(uint8_t &protoVer, uint8_t &fwMajor, uint8_t &fwMinor);
  virtual void tick(int64_t cycleCount);

  virtual uint32_t statFramesSent();
  virtual uint32_t statFramesReceived();
  virtual uint32_t statCrcErrors();
  virtual uint32_t statRetries();
  virtual uint32_t statTimeouts();

  // One-line MAC-RAW NAT diagnostic for the D_SHOWNET overlay: the active TCP
  // flow's state/window plus the backend slot state and cumulative bytes each way.
  void debugNetState(char *buf, uint16_t n);

 private:
  static TeensyUthernet2 *s_instance;
  static void frameCb(uint8_t type, uint8_t seq, const uint8_t *p, uint16_t len);
  void onFrame(uint8_t type, uint8_t seq, const uint8_t *p, uint16_t len);

  uint8_t nextSeq();

  // Async command engine: the ESP protocol is strict request/reply, so only one
  // command is outstanding at a time. issue() sends without waiting; pump()
  // advances the outstanding command WITHOUT blocking (reads whatever the UART
  // has buffered, matches the reply, retries on timeout). This lets a long reply
  // arrive over many tick() passes while the 6502 keeps running. command() is a
  // thin blocking wrapper for the infrequent control path (boot ping, WiFi join,
  // DNS), which never overlaps socket data.
  bool cmdIdle() const { return !cmdInFlight; }
  bool cmdDone() const { return cmdDoneFlag; }  // last command finished (ok or not)
  bool cmdOk()   const { return cmdOkFlag; }     // finished with a matching reply
  // owner: 0 = control path (reply left in rp* for command()); 1 = UnBackendEsp
  // (on completion pump() dispatches the reply to unEsp.onCommandDone(tag)).
  bool issue(uint8_t type, const uint8_t *payload, uint16_t len,
             uint32_t timeoutMs, uint8_t owner = 0, uint8_t tag = 0);
  void pump();
  void completeCommand(bool ok); // finish the in-flight command + dispatch
  bool command(uint8_t type, const uint8_t *payload, uint16_t len,
               uint32_t timeoutMs = 200);
  bool pingOnce();   // one link probe (sets linkUp on reply)
  bool probeLink();  // throttled re-probe while the link is down

  Stream     *link;
  FrameParser parser;
  bool        linkUp;
  uint32_t    lastProbe;   // millis of the last link re-probe while down
  uint32_t    lastServiceMs; // millis of the last ESP socket-service pass
  uint8_t     seq;

  // link health counters (frames received / CRC errors come from the parser)
  uint32_t    cTx;         // command frames sent, including retries
  uint32_t    cRetries;    // resend attempts
  uint32_t    cTimeouts;   // commands that gave up after all retries

  // Captured reply of the outstanding command.
  bool     rpGot;
  uint8_t  rpType;
  uint8_t  rpSeq;
  uint16_t rpLen;
  uint8_t  rpBuf[AIIE_ESP_MAX_PAYLOAD];

  // Async command-engine bookkeeping (see issue()/pump()).
  bool     cmdInFlight;   // a command is out, awaiting its reply
  bool     cmdDoneFlag;   // the last-issued command has completed
  bool     cmdOkFlag;     // it completed with a matching reply (rp* valid)
  uint8_t  cmdType;
  uint8_t  cmdSeq;
  uint8_t  cmdTries;
  uint32_t cmdSentMs;
  uint32_t cmdTimeoutMs;
  uint16_t cmdPayLen;
  uint8_t  cmdOwner;      // 0 = control path, 1 = UnBackendEsp (dispatch on done)
  uint8_t  cmdTag;        // owner-defined tag (the ESP slot, for UnBackendEsp)
  uint8_t  cmdPayload[AIIE_ESP_MAX_PAYLOAD]; // copy kept for retries

  // Per-socket shadow state and local receive buffer.
  uint8_t  sr[U2_NUM_SOCKETS];
  uint8_t  proto[U2_NUM_SOCKETS];      // 0xFF when unused
  uint8_t  rxData[U2_NUM_SOCKETS][TU2_RXBUF];
  uint16_t rxLen[U2_NUM_SOCKETS];
  uint16_t rxOff[U2_NUM_SOCKETS];
  bool     rxHasSrc[U2_NUM_SOCKETS];
  uint8_t  rxSrcIp[U2_NUM_SOCKETS][4];
  uint16_t rxSrcPort[U2_NUM_SOCKETS];
  uint8_t  pollCursor;
  uint32_t sockNextPollMs[U2_NUM_SOCKETS]; // per-socket earliest next idle poll
  uint16_t sockPollIvMs[U2_NUM_SOCKETS];   // per-socket current poll interval

  char ssid[33];
  char pass[65];
  bool haveCreds;

  // MAC-RAW: the same user-mode NAT stack the SDL build uses, driven here by an
  // UnBackendEsp that maps onto this link's socket protocol. unEsp is declared
  // before usernet so it is constructed first.
  UnBackendEsp unEsp;
  UserNet      usernet;
  bool         macraw;   // true once the Apple opens socket 0 in MAC-RAW mode
};

#endif
