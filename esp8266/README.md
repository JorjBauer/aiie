# aiie ESP-01 network co-processor

Firmware that turns an ESP8266 (ESP-01 / ESP-01S) into the network back end for
the Teensy-side emulation of the Wiznet **W5100 / Uthernet II**. The Teensy
emulates the W5100 register/buffer model that Apple II software talks to over
the `$C0x4-7` soft switches; this firmware provides the real TCP/IP via the
ESP8266's lwIP stack, driven by a compact binary UART protocol.

- **Protocol:** see [`PROTOCOL.md`](PROTOCOL.md). Shared constants live in
  [`protocol.h`](protocol.h) (dependency-free - include it from the Teensy build
  too).
- **Framing:** [`frame.h`](frame.h) / [`frame.cpp`](frame.cpp).
- **Firmware:** [`esp8266.ino`](esp8266.ino).

## Feature status

| W5100 mode | Status |
|---|---|
| TCP (client + single-listener server) | ✅ implemented |
| UDP | ✅ implemented |
| IP-RAW (ICMP/ping, raw IP proto) | 🚧 stubbed → lwIP raw PCB (`ENOTIMPL`) |
| MAC-RAW (own-stack: IP65/Marinetti/Contiki/A2osX) | 🚧 stubbed → NAPT netif (`ENOTIMPL`) |

Also implemented: WiFi join/leave/status, async DNS resolve, link ping, device
info, and the pull-based RX poll that carries flow control. The link is strictly
half-duplex and master-polled (the ESP is reply-only); see `PROTOCOL.md §0`. The
two raw paths are reserved and stubbed; MAC-RAW is a NAT router (not an L2
bridge) for the reasons in the top-level analysis.

## Wiring (aiie r9, Teensy 4.1)

From the board file (`schematics/aiie.brd` / `aiie.sch`, part `U$4` = ESP-01):

| ESP-01 pin | Teensy 4.1 | Notes |
|---|---|---|
| TXD (GPIO1) | **pin 19 / A5** | ESP → Teensy |
| RXD (GPIO3) | **pin 18 / A4** | Teensy → ESP |
| CH_PD (EN)  | via R4 | held for boot/enable |
| RST         | via R5 | reset |
| GPIO0       | via R3 (pull-up) | boots to run mode; hold LOW to flash |
| GPIO2       | not connected | - |

> ⚠️ **Teensy-side UART caveat.** Pins 18/19 (A4/A5) are the Teensy 4.1 **I²C**
> pins (SDA0/SCL0), *not* a hardware-UART pair - no `SerialN` maps to 18/19. The
> Teensy driver will need `SoftwareSerial` there, which is rough much above
> 115200 with no flow control. Note the `jorjlib:TEENSY41` footprint *labels*
> these pads `P$D14`/`P$D15`; pins **14/15** genuinely are `Serial3` (TX3=14,
> RX3=15), so if the intent was hardware serial, the pad labeling vs. physical
> routing is worth confirming before wiring the Teensy side. The ESP firmware is
> agnostic - it always uses UART0 (GPIO1/GPIO3).

## Build & flash

Requires `arduino-cli` and the `esp8266:esp8266` core (already present here at
1.3.1 / 3.1.2). One-time core install if starting fresh:

```sh
make setup
```

Compile:

```sh
make                 # -> build/ ; verified: ~253 KB flash, ~31 KB RAM on ESP-01S
```

Flash (needs a **3.3 V** USB-serial adapter, *not* the Teensy - GPIO0 isn't
Teensy-driven on r9, so program the module on the bench with GPIO0 held LOW at
reset to enter the bootloader):

```sh
make upload PORT=/dev/cu.usbserial-XXXX
make monitor PORT=/dev/cu.usbserial-XXXX     # optional
```

Board/flash size defaults to the ESP-01S (1 MB, `eesz=1M`). For a 512 KB ESP-01:

```sh
make BOARD_OPTS=eesz=512K64,baud=115200,xtal=80
```

Runtime UART speed to the Teensy defaults to 115200; override at build time:

```sh
make LINK_BAUD=230400
```

Run `make info` for the full list of valid board options.

## Teensy-side integration (next step, not in this dir)

The Teensy emulates the W5100 register model (`apple/uthernet2.cpp`) and
translates register/command writes into the commands in `PROTOCOL.md` via the
`TeensyUthernet2` backend (`teensy/teensy-uthernet2.cpp`):

- `Sn_CR` OPEN/CONNECT/LISTEN/SEND/CLOSE map to `CMD_SOCK_*`; each reply carries
  the resulting `sr`, which drops straight into the emulated `Sn_SR`.
- Serve the Apple's register reads from a local shadow; never block the 6502 on
  a link round-trip. Refresh the shadow from a service tick that issues
  `CMD_SOCK_POLL` with `maxlen` = the emulated RX ring's free space, and copy the
  returned bytes into the ring.
- Drive all link I/O from that one tick, and keep it bounded per call, since
  SoftwareSerial is half-duplex and slow.
- Bring the link up by polling `CMD_LINK_PING` (or `CMD_GET_INFO`) until it
  answers; the ESP sends nothing unsolicited.

Include `protocol.h` on both sides so the message/field/status/`SR` constants
never drift.
