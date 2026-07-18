#ifndef __UTHERNET2_H
#define __UTHERNET2_H

#ifdef TEENSYDUINO
#include <Arduino.h>
#else
#include <stdint.h>
#include <stdio.h>
#endif

#include "applemmu.h"
#include "slot.h"

/* Uthernet2 emulates the Apple-facing side of a W5100-based Ethernet card.
 *
 * The card presents four soft switches in its $C0nX I/O space:
 *   +0  mode register
 *   +1  address pointer, high byte
 *   +2  address pointer, low byte
 *   +3  data port (indirect access to the 32 KB W5100 address space)
 *
 * The hardware decodes only the low two address bits, so these four registers
 * mirror through the whole $C0nX window (also at +4..+7, +8..+B, +C..+F).
 * Different drivers use different mirrors, so the decode masks to two bits.
 *
 * The W5100 register and buffer model is emulated here, platform-independently:
 * common registers at $0000, four sockets of registers at $0400/$0500/$0600/
 * $0700, transmit buffers at $4000, receive buffers at $6000. The actual
 * network traffic is carried by g_uthernet, a Uthernet2Interface whose concrete
 * implementation is supplied by the platform (see sdl/ and teensy/). If
 * g_uthernet is NULL the card still answers register reads/writes, but no
 * packets move.
 */

// W5100 address space is 32 KB, accessed indirectly through the data port.
#define U2_MEM_SIZE 0x8000

// Soft-switch offsets within the card's $C0nX window. Only the low two address
// bits are decoded, so these mirror every four bytes across the window.
#define U2_SW_MASK     0x03
#define U2_SW_MODE     0x00
#define U2_SW_ADDR_HI  0x01
#define U2_SW_ADDR_LO  0x02
#define U2_SW_DATA     0x03

// Mode register bits.
#define U2_MR_IND  0x01  // indirect bus mode (always set in use)
#define U2_MR_AI   0x02  // auto-increment the address pointer after data access
#define U2_MR_RST  0x80  // soft reset

#define U2_SOCKETS 4

class Uthernet2 : public Slot {
 public:
  Uthernet2(AppleMMU *mmu);
  virtual ~Uthernet2();

  virtual bool Serialize(int8_t fd);
  virtual bool Deserialize(int8_t fd);

  virtual void Reset(); // cold-boot
  virtual uint8_t readSwitches(uint8_t s);
  virtual void writeSwitches(uint8_t s, uint8_t v);
  virtual void loadROM(uint8_t *toWhere);
  virtual bool hasRom() { return false; } // I/O-only card: no slot ROM

  // Called from the VM maintenance path to service async network I/O.
  void tick(int64_t cycleCount);

 private:
  uint8_t readByteAt(uint16_t addr);
  void writeByteAt(uint16_t addr, uint8_t v);
  void autoIncrement();

  uint16_t readMem16(uint16_t addr) const;   // big-endian, as the W5100 stores
  uint8_t socketProto(uint8_t sock) const;    // low nibble of Sn_MR
  uint8_t socketHeaderSize(uint8_t sock) const;

  void computeBufferSizes();
  void setCommand(uint8_t sock, uint8_t cmd);
  void doSend(uint8_t sock);
  void pullReceived(uint8_t sock);
  void serviceInterrupts(); // update per-socket and common interrupt state
  uint8_t socketIntrByte(uint8_t sock) const; // Sn_IR value (sticky bits + live RECV)
  void writeRxByte(uint8_t sock, uint8_t v);
  void writeRx16(uint8_t sock, uint16_t v);
  uint16_t computeRSR(uint8_t sock) const;
  uint16_t computeFSR(uint8_t sock) const;
  uint16_t socketRegBase(uint8_t sock) const;

  AppleMMU *mmu;

  uint8_t modeRegister;
  uint16_t addressPointer;
  uint8_t *memory; // U2_MEM_SIZE bytes, the emulated W5100 address space

  uint16_t txBase[U2_SOCKETS];
  uint16_t txSize[U2_SOCKETS];
  uint16_t rxBase[U2_SOCKETS];
  uint16_t rxSize[U2_SOCKETS];
  uint16_t rxWr[U2_SOCKETS]; // internal receive write pointer, per socket

  uint8_t socketIntr[U2_SOCKETS];  // sticky Sn_IR bits (CON/DISCON/etc.)
  uint8_t lastStatus[U2_SOCKETS];  // last observed status, to detect transitions
};

#endif
