#ifndef __ESPTRANSPORT_H
#define __ESPTRANSPORT_H

#include <stdint.h>

/* EspTransport is the one thing UnBackendEsp needs: a way to send a protocol
 * command to the ESP co-processor and get its reply, plus a clock. TeensyUthernet2
 * implements this over its UART link; a host test can implement it with a mock
 * ESP, so the NAT-over-ESP logic is testable off-hardware. */
class EspTransport {
 public:
  virtual ~EspTransport() {}

  // Send one command (CMD_*), block for its reply. Returns true on success and
  // fills rType/rBuf (up to rCap bytes)/rLen with the reply. Non-blocking on the
  // caller's data: the ESP answers every command promptly.
  virtual bool espCommand(uint8_t type, const uint8_t *payload, uint16_t len,
                          uint8_t &rType, uint8_t *rBuf, uint16_t rCap,
                          uint16_t &rLen, uint32_t timeoutMs) = 0;

  virtual uint32_t nowSecs() = 0;  // seconds clock (millis()/1000 on Teensy)
};

#endif
