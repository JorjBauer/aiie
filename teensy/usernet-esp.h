#ifndef __USERNET_ESP_H
#define __USERNET_ESP_H

#include "unbackend.h"
#include "esptransport.h"

/* UnBackendEsp implements UserNet's host-network operations over the ESP socket
 * protocol (CMD_SOCK_*). Unlike the original synchronous version, this one is
 * NON-BLOCKING: the backend methods only touch per-slot buffers/state and return
 * immediately, while service() (driven from TeensyUthernet2::tick()) issues at
 * most one ESP command at a time and onCommandDone() applies each reply. The ESP
 * protocol is strict request/reply, so a per-slot round-robin scheduler is the
 * whole "command queue" -- one command in flight, the emulator never blocks on a
 * UART round-trip.
 *
 * Handles are ESP socket indices (0..UNESP_SLOTS-1). */
#define UNESP_SLOTS 4
#define UNESP_RXBUF 1460   // one MTU of buffered receive per slot
#define UNESP_TXBUF 1460   // one segment of staged send per slot

class UnBackendEsp : public UnBackend {
 public:
  UnBackendEsp(EspTransport *t);

  // UnBackend surface -- all non-blocking; the actual ESP work happens in service().
  virtual int  tcpOpen();
  virtual bool tcpConnect(int h, const uint8_t ip[4], uint16_t port);
  virtual int  tcpConnectPoll(int h);
  virtual int  tcpSend(int h, const uint8_t *data, uint16_t len);
  virtual int  tcpRecv(int h, uint8_t *buf, uint16_t maxLen);
  virtual void tcpShutdownWrite(int h);

  virtual int  udpOpen(uint16_t bindPort);
  virtual int  udpSend(int h, const uint8_t ip[4], uint16_t port,
                       const uint8_t *data, uint16_t len);
  virtual int  udpRecv(int h, uint8_t *buf, uint16_t maxLen,
                       uint8_t srcIp[4], uint16_t *srcPort);

  virtual void sockClose(int h);
  virtual uint32_t nowSecs();

  virtual int tcpListen(uint16_t hostPort);
  virtual int tcpAccept(int listenH);

  // Scheduler hook (called from tick() each pass): issue the next slot's command
  // if the engine is idle. And the engine's completion callback.
  void service();
  void onCommandDone(uint8_t slot, bool ok, uint8_t rType,
                     const uint8_t *rBuf, uint16_t rLen);
  void reset();   // drop all slots (on a card/VM reset)

 private:
  enum { ROLE_FLOW = 0, ROLE_LISTEN = 1, ROLE_LISTEN_CONN = 2 };
  // Slot lifecycle phase = what the scheduler should issue next for the slot.
  enum { PH_FREE = 0, PH_OPEN, PH_CONNECT, PH_LISTEN, PH_READY, PH_CLOSE };
  // Op currently in flight for a slot (so onCommandDone knows how to apply it).
  enum { OP_NONE = 0, OP_OPEN, OP_CONNECT, OP_LISTEN, OP_POLL, OP_SEND, OP_CLOSE };

  struct Slot {
    bool     used;
    uint8_t  proto;    // AIIE_PROTO_TCP / AIIE_PROTO_UDP
    uint8_t  sr;       // cached W5100 status
    uint8_t  role;
    uint8_t  phase;
    uint8_t  op;       // OP_* in flight, else OP_NONE
    uint16_t lport;    // UDP bind / listen port

    bool     wantConnect;
    bool     connectFailed;
    uint8_t  connIp[4];
    uint16_t connPort;

    // receive buffer: filled by a POLL reply, drained by tcpRecv/udpRecv.
    uint16_t rxLen;
    uint16_t rxOff;
    bool     rxHasSrc;
    uint8_t  rxSrcIp[4];
    uint16_t rxSrcPort;
    uint8_t  rx[UNESP_RXBUF];

    // send staging: filled by tcpSend/udpSend, drained by a SEND.
    uint16_t txLen;
    bool     txHasDest;
    uint8_t  txDestIp[4];
    uint16_t txDestPort;
    uint8_t  tx[UNESP_TXBUF];
  } slots[UNESP_SLOTS];

  EspTransport *t;
  uint8_t schedCursor;

  int  allocSlot();
  void freeSlot(int h);
  bool issueFor(int h);  // issue this slot's next-needed command; true if issued
};

#endif
