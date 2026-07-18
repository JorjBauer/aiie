# aiie / ESP-01 UART wire protocol (v2)

This document specifies the binary protocol between the **Teensy** (which
emulates the Wiznet W5100 / Uthernet II register model that Apple II software
talks to) and the **ESP8266** (the real network back end). `protocol.h` is the
machine-readable source of truth for every constant named here; keep the two in
sync.

The division of labor:

```
Apple II sw --($C0x4-7)--> [ W5100 register+buffer model, on Teensy ]
                                     |  this protocol, over UART (see README for pins)
                                     v
                         [ ESP8266: WiFi + lwIP TCP/IP ] --> AP
```

The Teensy owns the W5100 semantics (address pointer, circular TX/RX buffers,
`Sn_SR`/`Sn_CR`, auto-increment). The ESP owns sockets and packets.

## 0. Half-duplex discipline (the rule everything else follows)

On the Teensy side the link is a SoftwareSerial, because the only free pins are
not a hardware-UART pair. SoftwareSerial cannot reliably receive while it is
transmitting. So the protocol is strictly **master/slave**:

- The **Teensy is the master**. It sends one command, then reads exactly one
  reply before sending the next.
- The **ESP is reply-only**. It transmits nothing on its own initiative. Every
  frame it sends is the immediate reply to a command, and echoes that command's
  `SEQ`.
- Because only one side ever talks at a time, the two transmit directions never
  overlap, and there is no need for hardware flow control.

Everything that a push-based design would deliver asynchronously (link-up, WiFi
association, socket-state changes, received data) is instead **discovered by the
Teensy polling**. There are no unsolicited frames.

## 1. Framing

Every message is one frame:

```
+------+------+--------+------+------+------------------+--------+
| SOF0 | SOF1 |  LEN   | TYPE | SEQ  |     PAYLOAD      |  CRC   |
| 0xA5 | 0x5A | u16 LE | u8   | u8   |  (LEN-2 bytes)   | u16 LE |
+------+------+--------+------+------+------------------+--------+
```

- **LEN** counts `TYPE + SEQ + PAYLOAD` (so payload length = `LEN - 2`). Max
  payload `AIIE_ESP_MAX_PAYLOAD` = 1600.
- **CRC** is CRC16-CCITT (poly `0x1021`, init `0xFFFF`) over `TYPE`, `SEQ` and
  `PAYLOAD`, i.e. exactly the `LEN` bytes, not `SOF`/`LEN`/`CRC` themselves.
- Framing integers (`LEN`, `CRC`) are **little-endian**. Integers inside
  payloads are **little-endian** too, with two exceptions that are always
  called out: **IPv4 addresses** travel as 4 raw octets `a.b.c.d` (network
  order), and **ports** are carried host-order u16 LE (the Teensy does the
  big-endian swap the W5100 registers want).
- **SEQ** is chosen by the Teensy and echoed by the ESP in the reply. Since the
  link is one command then one reply, `SEQ` is mainly a sanity check: after a
  timeout and resync the Teensy can discard a reply whose `SEQ` does not match
  the outstanding command.
- **Resync:** a receiver that loses sync hunts for the `SOF0,SOF1` pair, reads
  `LEN`, and validates `CRC`. A bad CRC or an implausible `LEN`
  (`> MAX_PAYLOAD+2`) drops back to hunting.

## 2. Message types

Convention: `0x00-0x7F` = Teensy to ESP (commands); `0x80-0xFF` = ESP to Teensy
(replies). Every command yields exactly one reply.

| Type | Name | Reply | Payload |
|------|------|-------|---------|
| 0x01 | `CMD_RESET`       | `EVT_ACK`        | *(none)* |
| 0x02 | `CMD_LINK_PING`   | `EVT_LINK_PONG`  | opaque echo bytes |
| 0x03 | `CMD_WIFI_JOIN`   | `EVT_ACK`        | `ssidLen u8, ssid[], pwLen u8, pw[]` |
| 0x04 | `CMD_WIFI_LEAVE`  | `EVT_ACK`        | *(none)* |
| 0x05 | `CMD_GET_INFO`    | `EVT_INFO`       | *(none)* |
| 0x06 | `CMD_WIFI_STATUS` | `EVT_WIFI`       | *(none)* |
| 0x07 | `CMD_DNS_RESOLVE` | `EVT_ACK`        | `hostname[]` (starts async lookup) |
| 0x08 | `CMD_DNS_RESULT`  | `EVT_DNS`        | *(none)* (polls the lookup) |
| 0x10 | `CMD_SOCK_OPEN`   | `EVT_SOCK_STATE` | `sock u8, proto u8, ipproto u8, lport u16` |
| 0x11 | `CMD_SOCK_CONNECT`| `EVT_SOCK_STATE` | `sock u8, dip[4], dport u16` |
| 0x12 | `CMD_SOCK_LISTEN` | `EVT_SOCK_STATE` | `sock u8, lport u16` |
| 0x13 | `CMD_SOCK_SEND`   | `EVT_SOCK_SENT`  | `sock u8, flags u8, [dip[4],dport u16], data[]` |
| 0x14 | `CMD_SOCK_CLOSE`  | `EVT_SOCK_STATE` | `sock u8` |
| 0x15 | `CMD_SOCK_POLL`   | `EVT_SOCK_DATA`  | `sock u8, maxlen u16` |
| 0x20 | `CMD_RAW_CONFIG`  | `EVT_ACK`        | *(reserved, raw path)* |
| 0x21 | `CMD_RAW_SEND`    | `EVT_ACK`        | *(reserved)* one outbound frame |
| 0x22 | `CMD_RAW_POLL`    | `EVT_RAW_FRAME`  | *(reserved)* `maxlen u16` |
| 0x80 | `EVT_ACK`         | | `status u8` |
| 0x81 | `EVT_ERROR`       | | `status u8, msg[]` (ascii, optional) |
| 0x82 | `EVT_LINK_PONG`   | | echoes `CMD_LINK_PING` payload |
| 0x83 | `EVT_INFO`        | | `protoVer, fwMaj, fwMin, wifiState, mac[6], ip[4]` |
| 0x84 | `EVT_WIFI`        | | `state u8 (0/1), ip[4]` |
| 0x85 | `EVT_DNS`         | | `pending u8, ip[4]` |
| 0x86 | `EVT_SOCK_STATE`  | | `sock u8, sr u8` |
| 0x87 | `EVT_SOCK_DATA`   | | `sock u8, sr u8, remain u16, flags u8, [sip[4],sport u16], data[]` |
| 0x88 | `EVT_SOCK_SENT`   | | `sock u8, accepted u16` |
| 0x89 | `EVT_RAW_FRAME`   | | *(reserved)* `remain u16, frame[]` |

### `status` codes (`AIIE_ESP_ST_*`)
`0 OK`, `1 EBADARG`, `2 EBADSOCK`, `3 ENOTCONN`, `4 EWIFI`, `5 EAGAIN`,
`6 ENOTIMPL`, `7 EINTERNAL`. A command may reply `EVT_ERROR` with one of these
in place of its normal reply.

### `proto` field of `CMD_SOCK_OPEN` (`AIIE_PROTO_*`)
`0 TCP`, `1 UDP`, `2 IPRAW`, `3 MACRAW`. `ipproto` is the IP protocol number,
used only for `IPRAW`. `lport` is used for UDP and for a TCP server.

### `sr` values (`W5100_SR_*`)
Carried verbatim in `EVT_SOCK_STATE` and `EVT_SOCK_DATA` so the Teensy drops the
byte straight into `Sn_SR`:
`CLOSED 0x00, INIT 0x13, LISTEN 0x14, SYNSENT 0x15, ESTABLISHED 0x17,
CLOSE_WAIT 0x1C, UDP 0x22, IPRAW 0x32, MACRAW 0x42`.

## 3. Socket lifecycle (how W5100 ops map)

The Teensy translates writes to the emulated `Sn_MR`/`Sn_CR`/buffer pointers
into commands; each socket command returns the resulting `sr` in its reply, so
the Teensy always learns the new state immediately.

**TCP client**
1. Apple sets `Sn_MR = TCP`, issues `OPEN`: `CMD_SOCK_OPEN(TCP)` -> `sr = INIT`.
2. Apple sets `Sn_DIPR`/`Sn_DPORT`, issues `CONNECT`: `CMD_SOCK_CONNECT` -> `sr = ESTABLISHED` (or `CLOSED`).
3. Apple writes TX buffer + `SEND`: `CMD_SOCK_SEND`; the reply says how many bytes were accepted.
4. Apple polls `Sn_RX_RSR`: the Teensy issues `CMD_SOCK_POLL`; see section 4.
5. Apple `CLOSE` or peer FIN: `CMD_SOCK_CLOSE` -> `CLOSED`, or a poll returns `CLOSE_WAIT` once the peer has closed and buffered data is drained.

**TCP server:** `CMD_SOCK_LISTEN` -> `LISTEN`; a later poll returns `ESTABLISHED`
once a client connects. One accepted client per listening socket.

**UDP:** `CMD_SOCK_OPEN(UDP, lport)` -> `UDP`. Each `CMD_SOCK_SEND` sets
`flags = HAS_DEST` and carries `dip/dport` (this mirrors the W5100 "write
`Sn_DIPR`/`Sn_DPORT`, then `SEND`" idiom). Received datagrams come back through
`CMD_SOCK_POLL` with `flags = HAS_SRC` and `sip/sport`, which the Teensy uses to
build the W5100's 8-byte UDP RX header (`IP[4] port[2] len[2]`).

The `CMD_SOCK_CONNECT` reply is where the ESP may block briefly (a bounded TCP
connect), and only when a socket is being opened. Every other command replies
promptly.

## 4. Polling for received data (this replaces push and flow control)

Apple software polls the W5100 (`Sn_SR`, `Sn_RX_RSR`) constantly. The Teensy
must serve those register reads from a **local shadow** and never block the
6502 on a link round-trip. It refreshes that shadow by issuing `CMD_SOCK_POLL`
from its own service tick at whatever cadence the SoftwareSerial allows.

`CMD_SOCK_POLL` carries `maxlen`, the number of bytes the Teensy can accept for
that socket right now (the free space in the emulated RX ring). That single
value is the flow control: the ESP never returns more than `maxlen`. The reply,
`EVT_SOCK_DATA`, carries:

- `sr`: the current socket status (so a poll doubles as a status read),
- `remain`: bytes still buffered on the ESP for this socket after this reply. A
  non-zero `remain` means "poll again"; `maxlen = 0` yields a status-only read.
- `flags` + optional `sip/sport`, then the data.

For **TCP** the data is a stream slice of up to `min(maxlen, RX_CHUNK)` bytes.
For **UDP/IPRAW** the ESP delivers one whole datagram, and only if it fits in
`maxlen`; if it does not fit, the reply carries zero data bytes and sets
`remain` to the datagram's size, so the Teensy knows to poll again once its ring
has room. Datagrams are never split, matching the W5100's atomic UDP RX header.

The ESP buffers incoming data in a per-socket staging buffer between polls, so
nothing is lost between the moment it arrives and the moment the Teensy asks for
it. TX flow is naturally bounded too: the Apple cannot issue `SEND` faster than
the `EVT_SOCK_SENT` replies come back.

## 5. Boot and link bring-up

The ESP sends nothing at boot (that would violate half-duplex). The Teensy
brings the link up by polling: it issues `CMD_LINK_PING` (or `CMD_GET_INFO`)
until it gets a valid reply, then treats the link as up. WiFi association is
likewise discovered by polling `CMD_WIFI_STATUS` until `EVT_WIFI` reports up.

## 6. DNS is asynchronous

The W5100 has no DNS; standard socket software resolves names itself over a UDP
socket to port 53, which flows through this protocol as ordinary UDP traffic. A
convenience resolver is provided for a future virtual-DNS mode, and it is split
in two so it never stalls the link:

- `CMD_DNS_RESOLVE(hostname)` starts a non-blocking lookup and replies `EVT_ACK`
  at once.
- `CMD_DNS_RESULT` polls it: `EVT_DNS` returns `pending = 1` while the lookup is
  in flight, or `pending = 0` with the resolved `ip` (`0.0.0.0` on failure).

## 7. The raw path (IP-RAW and MAC-RAW), reserved

`CMD_RAW_*`, `AIIE_PROTO_IPRAW`, and `AIIE_PROTO_MACRAW` are reserved and
currently answered with `ENOTIMPL`. They keep the same polled shape as the
socket path: outbound frames go up with `CMD_RAW_SEND`, inbound frames are
pulled with `CMD_RAW_POLL` returning `EVT_RAW_FRAME`.

**IP-RAW** (`Sn_MR = IPRAW`, e.g. ICMP): back the socket with an lwIP raw PCB
(`raw_new(ipproto)`), reusing `CMD_SOCK_SEND`/`CMD_SOCK_POLL` with the peer IP.

**MAC-RAW** (`Sn_MR = MACRAW`, socket 0): the mode software that runs its own
TCP/IP stack uses, pushing whole Ethernet frames. It cannot be bridged onto
WiFi (a station may not source foreign MACs to an AP), so the workable model is
a NAT router on the ESP: feed the Apple's frames into a custom lwIP netif,
answer its ARP and DHCP, and route/NAPT the traffic out over WiFi, returning
frames the same way. See the top-level analysis for the inherent limits.
