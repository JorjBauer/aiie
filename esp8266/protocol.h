/* aiie <-> ESP-01 UART wire protocol
 *
 * This header is the single source of truth for the binary protocol spoken
 * over the UART between the Teensy (which emulates the Wiznet W5100 / Uthernet
 * II register model) and the ESP8266 (which is the real network back end).
 *
 * It is deliberately dependency-free (pure #defines and a couple of small
 * inline helpers) so the *same* file can be included by both the ESP8266
 * Arduino sketch and the Teensy build.  Keep it that way.
 *
 * The link is half-duplex: on the Teensy side it is a SoftwareSerial, which
 * cannot reliably receive while it is transmitting.  So the protocol is
 * strictly MASTER/SLAVE: the Teensy sends one command and then reads exactly
 * one reply (matched by SEQ) before sending the next; the ESP NEVER transmits
 * except as the immediate reply to a command.  There are no unsolicited
 * frames.  Received socket data is pulled by the Teensy with CMD_SOCK_POLL,
 * not pushed, which also serves as the flow-control mechanism (the poll says
 * how many bytes the Teensy can currently accept).
 *
 * See PROTOCOL.md for the narrative description and rationale.
 */
#ifndef AIIE_ESP_PROTOCOL_H
#define AIIE_ESP_PROTOCOL_H

#include <stdint.h>
#include <stddef.h>

/* ---- versioning -------------------------------------------------------- */
#define AIIE_ESP_PROTO_VERSION   2   /* v2: half-duplex, master-polled */

/* ---- framing ----------------------------------------------------------- *
 * On the wire, every message is:
 *
 *   +------+------+--------+------+------+------------------+--------+
 *   | SOF0 | SOF1 |  LEN   | TYPE | SEQ  |     PAYLOAD      |  CRC   |
 *   | 0xA5 | 0x5A | u16 LE | u8   | u8   |  (LEN-2 bytes)   | u16 LE |
 *   +------+------+--------+------+------+------------------+--------+
 *
 *   LEN  = number of bytes in TYPE + SEQ + PAYLOAD (i.e. LEN-2 payload bytes)
 *   CRC  = CRC16-CCITT (poly 0x1021, init 0xFFFF) computed over
 *          TYPE, SEQ and PAYLOAD (everything LEN counts), not over LEN/SOF.
 *
 * All multi-byte integer fields in the framing (LEN, CRC) are little-endian.
 * Integer fields *inside* payloads are little-endian too, EXCEPT IPv4
 * addresses, which are carried as 4 raw octets in network order (a.b.c.d),
 * and are documented as such below.  Ports inside payloads are u16 LE (host
 * order); the Teensy is responsible for the big-endian swap the W5100 wants.
 *
 * SEQ correlates a command with its reply: the Teensy picks SEQ, the ESP
 * echoes the same SEQ in the reply.  Because the link is strictly one command
 * then one reply, SEQ is mostly a sanity check, but it lets the Teensy discard
 * a stale reply after a timeout/resync.
 *
 * Resync: a receiver that loses framing scans the byte stream for the SOF0,
 * SOF1 pair, reads LEN, then validates CRC.  A bad CRC or an implausible LEN
 * (> AIIE_ESP_MAX_PAYLOAD+2) drops back to hunting for SOF0.
 */
#define AIIE_ESP_SOF0            0xA5
#define AIIE_ESP_SOF1            0x5A

/* Largest payload we will send/accept.  1600 comfortably holds a 1518-byte
 * Ethernet frame (MAC-RAW path) plus a small per-message header. */
#define AIIE_ESP_MAX_PAYLOAD     1600
#define AIIE_ESP_FRAME_OVERHEAD  8   /* SOF0 SOF1 LEN(2) TYPE SEQ ... CRC(2) */
#define AIIE_ESP_MAX_FRAME       (AIIE_ESP_MAX_PAYLOAD + AIIE_ESP_FRAME_OVERHEAD)

/* ---- message types ----------------------------------------------------- *
 * Convention: 0x00-0x7F = Teensy -> ESP (commands),
 *             0x80-0xFF = ESP -> Teensy (replies).
 * EVERY command produces exactly one reply.  The default reply is EVT_ACK;
 * commands with a richer reply are noted below.  The ESP sends nothing on its
 * own initiative: link-up, WiFi association, socket-state changes and received
 * data are all discovered by the Teensy polling.
 */

/* -- control (Teensy -> ESP) -- */
#define CMD_RESET          0x01  /* -> EVT_ACK.   Drop all sockets, re-init.    */
#define CMD_LINK_PING      0x02  /* -> EVT_LINK_PONG. Echo; also link-up probe. */
#define CMD_WIFI_JOIN      0x03  /* -> EVT_ACK.   ssidlen u8, ssid[], pwlen, pw */
#define CMD_WIFI_LEAVE     0x04  /* -> EVT_ACK.                                 */
#define CMD_GET_INFO       0x05  /* -> EVT_INFO.                                */
#define CMD_WIFI_STATUS    0x06  /* -> EVT_WIFI.                                */
#define CMD_DNS_RESOLVE    0x07  /* -> EVT_ACK.   Start async lookup. hostname[]*/
#define CMD_DNS_RESULT     0x08  /* -> EVT_DNS.   Poll the async lookup result. */

/* -- sockets (Teensy -> ESP). Map 1:1 to the four W5100 sockets, index 0-3 -- */
#define CMD_SOCK_OPEN      0x10  /* -> EVT_SOCK_STATE. sock,proto,ipproto,lport */
#define CMD_SOCK_CONNECT   0x11  /* -> EVT_SOCK_STATE. sock, dip[4], dport      */
#define CMD_SOCK_LISTEN    0x12  /* -> EVT_SOCK_STATE. sock, lport  (TCP server)*/
#define CMD_SOCK_SEND      0x13  /* -> EVT_SOCK_SENT.  sock,flags,[dip,dport],..*/
#define CMD_SOCK_CLOSE     0x14  /* -> EVT_SOCK_STATE. sock                     */
#define CMD_SOCK_POLL      0x15  /* -> EVT_SOCK_DATA.  sock, maxlen u16         */

/* -- raw / MAC-RAW (Teensy -> ESP). See PROTOCOL.md. Reserved for now. -- */
#define CMD_RAW_CONFIG     0x20  /* -> EVT_ACK.        appleMac[6], subnet, ... */
#define CMD_RAW_SEND       0x21  /* -> EVT_ACK.        one outbound Eth frame   */
#define CMD_RAW_POLL       0x22  /* -> EVT_RAW_FRAME.  maxlen u16               */

/* -- replies (ESP -> Teensy). SEQ echoes the command that triggered them. -- */
#define EVT_ACK            0x80  /* status u8 (AIIE_ESP_ST_*)                   */
#define EVT_ERROR          0x81  /* status u8, msg[] (ascii, optional)          */
#define EVT_LINK_PONG      0x82  /* echoes CMD_LINK_PING payload                */
#define EVT_INFO           0x83  /* see EVT_INFO layout below                   */
#define EVT_WIFI           0x84  /* state u8 (0 down/1 up), ip[4]               */
#define EVT_DNS            0x85  /* pending u8, ip[4]  (0.0.0.0 == failed/none) */
#define EVT_SOCK_STATE     0x86  /* sock u8, sr u8 (W5100 SR code)              */
#define EVT_SOCK_DATA      0x87  /* sock, sr, remain u16, flags, [src], data[]  */
#define EVT_SOCK_SENT      0x88  /* sock u8, accepted u16                       */
#define EVT_RAW_FRAME      0x89  /* remain u16, frame[]  (raw path)             */

/* EVT_INFO payload layout:
 *   proto_ver u8, fw_major u8, fw_minor u8, wifi_state u8,
 *   mac[6], ip[4]
 *
 * EVT_DNS payload layout (reply to CMD_DNS_RESULT):
 *   pending u8 (1 = still resolving, 0 = done), ip[4] (valid when pending==0;
 *   0.0.0.0 means resolution failed).
 *
 * EVT_SOCK_DATA payload layout (reply to CMD_SOCK_POLL):
 *   sock u8, sr u8 (current W5100 status), remain u16 (bytes still buffered on
 *   the ESP for this socket AFTER this reply; a >0 value means "poll again"),
 *   flags u8 (AIIE_DATA_HAS_SRC for UDP/IPRAW), [sip[4], sport u16], data[].
 *   For UDP/IPRAW the data is one whole datagram; if it does not fit in the
 *   requested maxlen the ESP returns zero data bytes and sets remain to the
 *   datagram's size, so the Teensy knows to poll again with a larger window.
 */

/* ---- CMD_SOCK_OPEN proto field ---------------------------------------- */
#define AIIE_PROTO_TCP     0x00
#define AIIE_PROTO_UDP     0x01
#define AIIE_PROTO_IPRAW   0x02  /* lwIP raw PCB (ICMP etc.) - see PROTOCOL.md */
#define AIIE_PROTO_MACRAW  0x03  /* NAPT netif path          - see PROTOCOL.md */

/* ---- CMD_SOCK_SEND / EVT_SOCK_DATA flags ------------------------------ */
#define AIIE_SEND_HAS_DEST 0x01  /* payload carries dip[4],dport (UDP/IPRAW)    */
#define AIIE_DATA_HAS_SRC  0x01  /* payload carries sip[4],sport (UDP/IPRAW)    */

/* ---- status codes (EVT_ACK / EVT_ERROR) ------------------------------- */
#define AIIE_ESP_ST_OK        0x00
#define AIIE_ESP_ST_EBADARG   0x01
#define AIIE_ESP_ST_EBADSOCK  0x02
#define AIIE_ESP_ST_ENOTCONN  0x03
#define AIIE_ESP_ST_EWIFI     0x04
#define AIIE_ESP_ST_EAGAIN    0x05
#define AIIE_ESP_ST_ENOTIMPL  0x06  /* stubbed feature (IPRAW/MACRAW for now)   */
#define AIIE_ESP_ST_EINTERNAL 0x07

/* ---- W5100 socket-status (SR) codes -----------------------------------
 * EVT_SOCK_STATE and EVT_SOCK_DATA carry these verbatim so the Teensy can drop
 * the byte straight into the emulated Sn_SR register.  Values match the Wiznet
 * W5100 datasheet. */
#define W5100_SR_CLOSED      0x00
#define W5100_SR_INIT        0x13
#define W5100_SR_LISTEN      0x14
#define W5100_SR_SYNSENT     0x15
#define W5100_SR_ESTABLISHED 0x17
#define W5100_SR_CLOSE_WAIT  0x1C
#define W5100_SR_UDP         0x22
#define W5100_SR_IPRAW       0x32
#define W5100_SR_MACRAW      0x42

#define AIIE_ESP_NUM_SOCKETS 4

/* ---- CRC16-CCITT (0x1021, init 0xFFFF), incremental ------------------- */
static inline uint16_t aiie_crc16_update(uint16_t crc, uint8_t b) {
    crc ^= (uint16_t)b << 8;
    for (uint8_t i = 0; i < 8; ++i)
        crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    return crc;
}

static inline uint16_t aiie_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) crc = aiie_crc16_update(crc, data[i]);
    return crc;
}

#endif /* AIIE_ESP_PROTOCOL_H */
