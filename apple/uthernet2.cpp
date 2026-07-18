#include "uthernet2.h"
#include <string.h>

#include "globals.h"
#include "uthernet2interface.h"

#ifdef TEENSYDUINO
#include "teensy-println.h"
#endif

// ---- W5100 memory map ----------------------------------------------------
#define W5100_MR      0x0000
#define W5100_IR      0x0015  // common Interrupt Register (socket bits 0..3)
#define W5100_IMR     0x0016  // common Interrupt Mask Register
#define W5100_RTR0    0x0017  // Retry Time-value Register (reset default 0x07D0)
#define W5100_RTR1    0x0018
#define W5100_RCR     0x0019  // Retry Count Register (reset default 0x08)
#define W5100_RMSR    0x001A
#define W5100_TMSR    0x001B
#define W5100_PTIMER  0x0028  // PPP LCP Request Timer (reset default 0x28)
#define W5100_SIPR0   0x000F

#define W5100_S0_BASE 0x0400
#define W5100_TX_BASE 0x4000
#define W5100_RX_BASE 0x6000

// Socket register offsets (relative to a socket's base).
#define Sn_MR      0x00
#define Sn_CR      0x01
#define Sn_IR      0x02
#define Sn_SR      0x03

// Socket interrupt register (Sn_IR) bits.
#define SnIR_CON      0x01  // TCP connection established
#define SnIR_DISCON   0x02  // TCP connection closed by peer
#define SnIR_RECV     0x04  // data received (computed live from Sn_RX_RSR)
#define SnIR_TIMEOUT  0x08  // ARP/TCP timeout
#define SnIR_SENDOK   0x10  // send complete
#define Sn_PORT0   0x04
#define Sn_PROTO   0x14
#define Sn_DIPR0   0x0C
#define Sn_DPORT0  0x10
#define Sn_TX_FSR0 0x20
#define Sn_TX_RD0  0x22
#define Sn_TX_WR0  0x24
#define Sn_RX_RSR0 0x26
#define Sn_RX_RD0  0x28

// Socket command register values.
#define CR_OPEN    0x01
#define CR_LISTEN  0x02
#define CR_CONNECT 0x04
#define CR_DISCON  0x08
#define CR_CLOSE   0x10
#define CR_SEND    0x20
#define CR_RECV    0x40

// Low nibble of Sn_MR (W5100 protocol codes).
#define MR_PROTO_MASK 0x0F
#define MR_CLOSED     0x00
#define MR_TCP        0x01
#define MR_UDP        0x02
#define MR_IPRAW      0x03
#define MR_MACRAW     0x04

Uthernet2::Uthernet2(AppleMMU *mmu)
{
  this->mmu = mmu;
  memory = new uint8_t[U2_MEM_SIZE];
  Reset();
}

Uthernet2::~Uthernet2()
{
  delete[] memory;
}

bool Uthernet2::Serialize(int8_t fd)
{
  // TODO: persist modeRegister, addressPointer, and the register/buffer memory.
  return true;
}

bool Uthernet2::Deserialize(int8_t fd)
{
  // TODO: restore the state written by Serialize().
  return true;
}

uint16_t Uthernet2::socketRegBase(uint8_t sock) const
{
  return W5100_S0_BASE + (sock << 8);
}

uint16_t Uthernet2::readMem16(uint16_t addr) const
{
  // W5100 stores 16-bit values big-endian (high byte at the lower address).
  return ((uint16_t)memory[addr] << 8) | memory[addr + 1];
}

uint8_t Uthernet2::socketProto(uint8_t sock) const
{
  return memory[socketRegBase(sock) + Sn_MR] & MR_PROTO_MASK;
}

uint8_t Uthernet2::socketHeaderSize(uint8_t sock) const
{
  switch (socketProto(sock)) {
  case MR_UDP:   return 4 + 2 + 2; // source IP + port + length
  case MR_IPRAW: return 4 + 2;     // source IP + length
  default:       return 0;         // TCP has no per-read header
  }
}

void Uthernet2::computeBufferSizes()
{
  // Each 2-bit field of RMSR/TMSR sets a socket's buffer size; the buffers are
  // laid out consecutively from the RX/TX base addresses.
  uint8_t rmsr = memory[W5100_RMSR];
  uint8_t tmsr = memory[W5100_TMSR];
  uint16_t rb = W5100_RX_BASE;
  uint16_t tb = W5100_TX_BASE;
  for (uint8_t i = 0; i < U2_SOCKETS; i++) {
    uint16_t rs = 1 << (10 + (rmsr & 0x03));
    uint16_t ts = 1 << (10 + (tmsr & 0x03));
    rmsr >>= 2;
    tmsr >>= 2;
    // Clamp so the sum never runs past the end of its region (8 KB each).
    if (rb + rs > U2_MEM_SIZE)   rs = U2_MEM_SIZE - rb;
    if (tb + ts > W5100_RX_BASE) ts = W5100_RX_BASE - tb;
    rxBase[i] = rb; rxSize[i] = rs; rb += rs;
    txBase[i] = tb; txSize[i] = ts; tb += ts;
  }
}

void Uthernet2::Reset()
{
  modeRegister = 0;
  addressPointer = 0;
  memset(memory, 0, U2_MEM_SIZE);
  // W5100 register reset defaults. Some drivers (e.g. Contiki) probe for the
  // chip by reading these back, so they must match the datasheet exactly.
  memory[W5100_RTR0] = 0x07;  // Retry Time-value Register = 0x07D0 (2000)
  memory[W5100_RTR1] = 0xD0;
  memory[W5100_RCR]  = 0x08;  // Retry Count Register
  // PTIMER's real reset value marks this as a plain W5100 (not a DNS-offloading
  // "virtual W5100"): a 0x00 here makes offload-aware drivers try connect-by-
  // name, which this card does not implement.
  memory[W5100_PTIMER] = 0x28;
  // Default 2 KB per socket for both TX and RX (0x55 = 2 bits set per socket).
  memory[W5100_RMSR] = 0x55;
  memory[W5100_TMSR] = 0x55;
  computeBufferSizes();
  for (uint8_t i = 0; i < U2_SOCKETS; i++) {
    rxWr[i] = 0;
    socketIntr[i] = 0;
    lastStatus[i] = U2_SR_CLOSED;
  }
  if (g_uthernet) {
    g_uthernet->reset();
  }
}

void Uthernet2::autoIncrement()
{
  if (modeRegister & U2_MR_AI) {
    ++addressPointer;
    // The device wraps at the ends of the TX and RX buffer regions.
    if (addressPointer == W5100_RX_BASE || addressPointer == U2_MEM_SIZE) {
      addressPointer -= 0x2000;
    }
  }
}

uint16_t Uthernet2::computeRSR(uint8_t sock) const
{
  const uint16_t size = rxSize[sock];
  if (size == 0) return 0;
  const uint16_t mask = size - 1;
  const uint16_t rd = readMem16(socketRegBase(sock) + Sn_RX_RD0) & mask;
  const uint16_t wr = rxWr[sock] & mask;
  return (uint16_t)((wr - rd) & mask);
}

uint16_t Uthernet2::computeFSR(uint8_t sock) const
{
  const uint16_t size = txSize[sock];
  if (size == 0) return 0;
  const uint16_t mask = size - 1;
  const uint16_t rd = readMem16(socketRegBase(sock) + Sn_TX_RD0) & mask;
  const uint16_t wr = readMem16(socketRegBase(sock) + Sn_TX_WR0) & mask;
  const uint16_t present = (uint16_t)((wr - rd) & mask);
  return size - present;
}

void Uthernet2::writeRxByte(uint8_t sock, uint8_t v)
{
  const uint16_t size = rxSize[sock];
  const uint16_t base = rxBase[sock];
  memory[base + (rxWr[sock] % size)] = v;
  rxWr[sock] = (rxWr[sock] + 1) % size;
}

void Uthernet2::writeRx16(uint8_t sock, uint16_t v)
{
  writeRxByte(sock, (v >> 8) & 0xFF);
  writeRxByte(sock, v & 0xFF);
}

void Uthernet2::pullReceived(uint8_t sock)
{
  if (!g_uthernet) return;

  const uint8_t proto = socketProto(sock);
  if (proto != MR_TCP && proto != MR_UDP && proto != MR_IPRAW &&
      proto != MR_MACRAW) return;

  // MAC-RAW: one whole Ethernet frame, framed as [2-byte size][frame], where
  // the size includes the two header bytes (the format the driver reads back).
  if (proto == MR_MACRAW) {
    const uint16_t rsr = computeRSR(sock);
    const uint16_t size = rxSize[sock];
    if (size <= rsr) return;
    uint16_t freeRoom = size - rsr;
    if (freeRoom <= 3) return; // need room for the 2-byte header plus data
    uint16_t maxData = freeRoom - 2 - 1;
    if (maxData > 1522) maxData = 1522; // one Ethernet frame (with VLAN margin)

    uint8_t tmp[1522];
    int n = g_uthernet->recvRawFrame(tmp, maxData);
    if (n <= 0) return;
    writeRx16(sock, (uint16_t)(n + 2)); // size includes the 2 header bytes
    for (int i = 0; i < n; i++) writeRxByte(sock, tmp[i]);
    return;
  }

  const uint8_t header = socketHeaderSize(sock);
  const uint16_t rsr = computeRSR(sock);
  const uint16_t size = rxSize[sock];
  if (size <= rsr) return;
  uint16_t freeRoom = size - rsr;
  if (freeRoom <= (uint16_t)(header + 1)) return; // no room (and never fill fully)

  uint16_t maxData = freeRoom - header - 1;
  if (maxData > 1460) maxData = 1460;

  uint8_t tmp[1460];
  uint8_t srcIp[4] = {0, 0, 0, 0};
  uint16_t srcPort = 0;
  int n = g_uthernet->socketRecv(sock, tmp, maxData, srcIp, &srcPort);
  if (n <= 0) return;

  if (proto == MR_UDP) {
    writeRxByte(sock, srcIp[0]); writeRxByte(sock, srcIp[1]);
    writeRxByte(sock, srcIp[2]); writeRxByte(sock, srcIp[3]);
    writeRx16(sock, srcPort);
    writeRx16(sock, (uint16_t)n);
  } else if (proto == MR_IPRAW) {
    writeRxByte(sock, srcIp[0]); writeRxByte(sock, srcIp[1]);
    writeRxByte(sock, srcIp[2]); writeRxByte(sock, srcIp[3]);
    writeRx16(sock, (uint16_t)n);
  }
  for (int i = 0; i < n; i++) {
    writeRxByte(sock, tmp[i]);
  }
}

// Sn_IR value the driver sees: the sticky bits (CON/DISCON/...) plus a live
// RECV bit that reflects whether received data is currently buffered. RECV is
// deliberately not sticky: on the real chip it re-asserts while data remains,
// so it is a function of Sn_RX_RSR rather than a latched event.
uint8_t Uthernet2::socketIntrByte(uint8_t sock) const
{
  uint8_t ir = socketIntr[sock];
  if (computeRSR(sock) > 0) ir |= SnIR_RECV;
  return ir;
}

// Move any waiting inbound data into the RX buffers and latch connection-state
// transitions into the sticky interrupt bits. Called from the maintenance tick
// and whenever the driver reads the common Interrupt Register, so IR is never
// stale regardless of tick cadence.
void Uthernet2::serviceInterrupts()
{
  if (!g_uthernet) return;

  for (uint8_t sock = 0; sock < U2_SOCKETS; sock++) {
    const uint8_t proto = socketProto(sock);
    if (proto == MR_TCP || proto == MR_UDP || proto == MR_IPRAW ||
        proto == MR_MACRAW) {
      pullReceived(sock);
    }

    const uint8_t st = g_uthernet->socketStatus(sock);
    if (st != lastStatus[sock]) {
      if (st == U2_SR_ESTABLISHED) {
        socketIntr[sock] |= SnIR_CON; // connection came up
      } else if ((st == U2_SR_CLOSE_WAIT || st == U2_SR_CLOSED) &&
                 lastStatus[sock] == U2_SR_ESTABLISHED) {
        socketIntr[sock] |= SnIR_DISCON; // peer closed the connection
      }
      lastStatus[sock] = st;
    }
  }
}

void Uthernet2::doSend(uint8_t sock)
{
  if (!g_uthernet) return;

  const uint16_t regBase = socketRegBase(sock);
  const uint16_t size = txSize[sock];
  if (size == 0) return;
  const uint16_t mask = size - 1;
  const uint16_t rd = readMem16(regBase + Sn_TX_RD0) & mask;
  const uint16_t wr = readMem16(regBase + Sn_TX_WR0) & mask;
  uint16_t present = (uint16_t)((wr - rd) & mask);
  if (present == 0) return;

  static uint8_t buf[8192];
  if (present > sizeof(buf)) present = sizeof(buf);
  const uint16_t base = txBase[sock];
  for (uint16_t i = 0; i < present; i++) {
    buf[i] = memory[base + ((rd + i) & mask)];
  }

  if (socketProto(sock) == MR_MACRAW) {
    // MAC-RAW: the TX buffer holds one whole Ethernet frame, no per-peer addr.
    g_uthernet->sendRawFrame(buf, present);
  } else {
    uint8_t destIp[4];
    memcpy(destIp, memory + regBase + Sn_DIPR0, 4);
    uint16_t destPort = readMem16(regBase + Sn_DPORT0);
    g_uthernet->socketSend(sock, buf, present, destIp, destPort);
  }

  // Move the read pointer up to the write pointer (all of it consumed).
  const uint16_t newRd = readMem16(regBase + Sn_TX_WR0);
  memory[regBase + Sn_TX_RD0] = (newRd >> 8) & 0xFF;
  memory[regBase + Sn_TX_RD0 + 1] = newRd & 0xFF;
}

void Uthernet2::setCommand(uint8_t sock, uint8_t cmd)
{
  if (!g_uthernet) return;

  const uint16_t regBase = socketRegBase(sock);

  switch (cmd) {
  case CR_OPEN: {
    computeBufferSizes();
    rxWr[sock] = 0;
    socketIntr[sock] = 0;
    lastStatus[sock] = U2_SR_CLOSED;
    // Reset the socket's TX/RX pointers.
    memory[regBase + Sn_TX_RD0] = 0; memory[regBase + Sn_TX_RD0 + 1] = 0;
    memory[regBase + Sn_TX_WR0] = 0; memory[regBase + Sn_TX_WR0 + 1] = 0;
    memory[regBase + Sn_RX_RD0] = 0; memory[regBase + Sn_RX_RD0 + 1] = 0;

    uint8_t w5100proto = memory[regBase + Sn_MR] & MR_PROTO_MASK;
    uint8_t proto;
    switch (w5100proto) {
    case MR_TCP:    proto = U2_PROTO_TCP;    break;
    case MR_UDP:    proto = U2_PROTO_UDP;    break;
    case MR_IPRAW:  proto = U2_PROTO_IPRAW;  break;
    case MR_MACRAW: proto = U2_PROTO_MACRAW; break;
    default: return; // CLOSED / unknown: nothing to open
    }
    uint8_t ipproto = memory[regBase + Sn_PROTO];
    uint16_t localPort = readMem16(regBase + Sn_PORT0);
    g_uthernet->socketOpen(sock, proto, ipproto, localPort);
    break;
  }
  case CR_CONNECT: {
    uint8_t ip[4];
    memcpy(ip, memory + regBase + Sn_DIPR0, 4);
    uint16_t port = readMem16(regBase + Sn_DPORT0);
    g_uthernet->socketConnect(sock, ip, port);
    break;
  }
  case CR_LISTEN: {
    uint16_t localPort = readMem16(regBase + Sn_PORT0);
    g_uthernet->socketListen(sock, localPort);
    break;
  }
  case CR_SEND:
    doSend(sock);
    // The transfer completes synchronously, so raise SEND_OK right away. The
    // native Apple II drivers ignore this bit (they poll Sn_CR/Sn_TX_FSR), but
    // code ported from the Arduino/WIZnet libraries blocks on it after a SEND.
    socketIntr[sock] |= SnIR_SENDOK;
    break;
  case CR_RECV:
    // The host has advanced Sn_RX_RD; the received-size register is recomputed
    // from the pointers on the next read, so nothing else is needed here.
    break;
  case CR_CLOSE:
  case CR_DISCON:
    g_uthernet->socketClose(sock);
    break;
  default:
    break;
  }
}

uint8_t Uthernet2::readByteAt(uint16_t addr)
{
  addr &= (U2_MEM_SIZE - 1);

  // Common Interrupt Register: bit i set when socket i has a pending interrupt.
  // Reading it first services all sockets so the value is current.
  if (addr == W5100_IR) {
    serviceInterrupts();
    uint8_t ir = memory[W5100_IR] & 0xF0; // preserve any non-socket bits
    for (uint8_t s = 0; s < U2_SOCKETS; s++) {
      if (socketIntrByte(s)) ir |= (1 << s);
    }
    memory[W5100_IR] = ir;
    return ir;
  }

  if (addr >= W5100_S0_BASE && addr < W5100_TX_BASE) {
    const uint8_t sock = (addr >> 8) - 0x04;
    if (sock < U2_SOCKETS) {
      const uint8_t off = addr & 0xFF;
      switch (off) {
      case Sn_IR:
        pullReceived(sock); // refresh RECV visibility
        memory[addr] = socketIntrByte(sock);
        break;
      case Sn_SR:
        memory[addr] = g_uthernet ? g_uthernet->socketStatus(sock) : U2_SR_CLOSED;
        break;
      case Sn_RX_RSR0: {
        pullReceived(sock);
        uint16_t rsr = computeRSR(sock);
        memory[addr] = (rsr >> 8) & 0xFF;
        memory[addr + 1] = rsr & 0xFF;
        break;
      }
      case Sn_TX_FSR0: {
        uint16_t fsr = computeFSR(sock);
        memory[addr] = (fsr >> 8) & 0xFF;
        memory[addr + 1] = fsr & 0xFF;
        break;
      }
      default:
        break;
      }
    }
  }
  return memory[addr];
}

void Uthernet2::writeByteAt(uint16_t addr, uint8_t v)
{
  addr &= (U2_MEM_SIZE - 1);

  // Interrupt registers are write-1-to-clear; do not store the written value.
  if (addr == W5100_IR) {
    memory[W5100_IR] &= ~v;
    return;
  }
  if (addr >= W5100_S0_BASE && addr < W5100_TX_BASE &&
      (addr & 0xFF) == Sn_IR) {
    const uint8_t sock = (addr >> 8) - 0x04;
    if (sock < U2_SOCKETS) socketIntr[sock] &= ~v; // RECV is live, so unaffected
    return;
  }

  memory[addr] = v;

  if (addr == W5100_RMSR || addr == W5100_TMSR) {
    computeBufferSizes();
    return;
  }

  if (addr >= W5100_S0_BASE && addr < W5100_TX_BASE) {
    const uint8_t sock = (addr >> 8) - 0x04;
    const uint8_t off = addr & 0xFF;
    if (sock < U2_SOCKETS && off == Sn_CR) {
      setCommand(sock, v);
      memory[addr] = 0; // the real chip clears the command register when done
    }
  }
}

uint8_t Uthernet2::readSwitches(uint8_t s)
{
  s &= U2_SW_MASK; // registers mirror every four bytes across the window
  switch (s) {
  case U2_SW_MODE:
    return modeRegister;
  case U2_SW_ADDR_HI:
    return (addressPointer >> 8) & 0xFF;
  case U2_SW_ADDR_LO:
    return addressPointer & 0xFF;
  case U2_SW_DATA: {
    uint8_t v = readByteAt(addressPointer);
    autoIncrement();
    return v;
  }
  default:
    return 0;
  }
}

void Uthernet2::writeSwitches(uint8_t s, uint8_t v)
{
  s &= U2_SW_MASK; // registers mirror every four bytes across the window
  switch (s) {
  case U2_SW_MODE:
    if (v & U2_MR_RST) {
      Reset();
    } else {
      modeRegister = v;
    }
    break;
  case U2_SW_ADDR_HI:
    addressPointer = (addressPointer & 0x00FF) | ((uint16_t)v << 8);
    break;
  case U2_SW_ADDR_LO:
    addressPointer = (addressPointer & 0xFF00) | v;
    break;
  case U2_SW_DATA:
    writeByteAt(addressPointer, v);
    autoIncrement();
    break;
  default:
    break;
  }
}

void Uthernet2::loadROM(uint8_t *toWhere)
{
  // This card has no on-board ROM; present a blank slot ROM page so the boot
  // scan does not mistake it for a bootable peripheral.
  memset(toWhere, 0, 256);
}

void Uthernet2::tick(int64_t cycleCount)
{
  if (g_uthernet) {
    g_uthernet->tick(cycleCount);
    serviceInterrupts();
  }
}
