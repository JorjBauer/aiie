/*
 * aiie ESP-01 network co-processor
 * --------------------------------
 * Turns an ESP8266 (ESP-01 / ESP-01S) into the network back end for the
 * Teensy-side emulation of the Wiznet W5100 / Uthernet II.  The Teensy speaks
 * the binary framed protocol in protocol.h over a UART; this firmware maps it
 * onto the ESP8266's real TCP/IP stack (lwIP, via the Arduino WiFi classes).
 *
 * The link is half-duplex (SoftwareSerial on the Teensy), so this firmware is
 * strictly reply-only: it transmits ONLY as the immediate reply to a command,
 * never on its own initiative.  Received socket data is buffered here and
 * handed up when the Teensy asks for it with CMD_SOCK_POLL; that poll also
 * carries how many bytes the Teensy can take, which is the flow control.
 *
 * Wiring on aiie r9 (Teensy 4.1), per the board file:
 *   ESP TXD (GPIO1) -> Teensy pin 19 / A5
 *   ESP RXD (GPIO3) <- Teensy pin 18 / A4
 * On the ESP side the protocol travels on UART0 (GPIO1/GPIO3), the pins the
 * Arduino `Serial` object uses.
 *
 * Status of the four W5100 modes in THIS firmware:
 *   TCP    (AIIE_PROTO_TCP)    : implemented (client + single-listener server)
 *   UDP    (AIIE_PROTO_UDP)    : implemented
 *   IP-RAW (AIIE_PROTO_IPRAW)  : stubbed  -> lwIP raw PCB  (see handleOpen)
 *   MACRAW (AIIE_PROTO_MACRAW) : stubbed  -> NAPT netif    (see PROTOCOL.md)
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <lwip/dns.h>
#include "protocol.h"
#include "frame.h"

/* ---- configuration ----------------------------------------------------- */
#ifndef LINK_BAUD
#define LINK_BAUD 115200      /* UART speed to the Teensy. No HW flow control */
#endif
#define FW_MAJOR 0
#define FW_MINOR 2
#define STAGE     1460        /* per-socket RX staging buffer (one MTU)        */
#define RX_CHUNK  STAGE       /* max data bytes returned per CMD_SOCK_POLL     */

#define LINK Serial           /* UART0 == the GPIO1/GPIO3 pins                 */

/* ---- per-socket state -------------------------------------------------- */
struct Sock {
    uint8_t     proto = 0xFF;         /* AIIE_PROTO_* ; 0xFF == unused         */
    uint8_t     sr    = W5100_SR_CLOSED;
    WiFiClient  client;
    WiFiUDP     udp;
    WiFiServer *server = nullptr;     /* lazily created for TCP LISTEN          */

    uint8_t     rx[STAGE];            /* staged received bytes                  */
    uint16_t    rxLen = 0;            /* bytes valid in rx[]                    */
    uint16_t    rxOff = 0;            /* TCP: consume offset within rx[]        */
    bool        hasSrc = false;       /* UDP/IPRAW: rx[] holds one datagram     */
    uint8_t     srcIp[4] = {0, 0, 0, 0};
    uint16_t    srcPort = 0;
};
static Sock socks[AIIE_ESP_NUM_SOCKETS];

/* ---- async DNS (never blocks the link) --------------------------------- */
static bool     dnsPending = false;
static bool     dnsDone    = false;
static uint32_t dnsResult  = 0;       /* network-order IPv4, 0 == failed/none  */

static uint32_t ipaddr_v4(const ip_addr_t *a) {
#if LWIP_IPV6
    return ip_2_ip4(a)->addr;
#else
    return a->addr;
#endif
}

static void dnsCallback(const char *name, const ip_addr_t *ipaddr, void *arg) {
    (void)name; (void)arg;
    dnsResult = ipaddr ? ipaddr_v4(ipaddr) : 0;
    dnsDone = true;
    dnsPending = false;
}

/* ---- reply cache for retransmit (half-duplex retries) ------------------ *
 * The Teensy resends a command with the SAME seq when it does not get the
 * reply. To keep stateful commands (SEND, OPEN, ...) from executing twice, we
 * cache the last reply; when a command arrives carrying the seq we last
 * answered, we just resend the cached bytes instead of re-dispatching.
 */
static uint8_t  s_lastFrame[AIIE_ESP_MAX_FRAME];
static uint16_t s_lastFrameLen = 0;
static uint8_t  s_lastSeq = 0;
static bool     s_haveLast = false;

static void sendReply(uint8_t type, uint8_t seq, const uint8_t *payload, uint16_t len) {
    s_lastFrameLen = frameBuild(s_lastFrame, type, seq, payload, len);
    s_lastSeq = seq;
    s_haveLast = true;
    if (s_lastFrameLen) LINK.write(s_lastFrame, s_lastFrameLen);
}

/* ---- small helpers ----------------------------------------------------- */
static void replyAck(uint8_t seq, uint8_t status) {
    sendReply(EVT_ACK, seq, &status, 1);
}

static void replyErr(uint8_t seq, uint8_t status) {
    sendReply(EVT_ERROR, seq, &status, 1);
}

static void ipToBytes(const IPAddress &ip, uint8_t *out) {
    out[0] = ip[0]; out[1] = ip[1]; out[2] = ip[2]; out[3] = ip[3];
}

static void replyState(uint8_t seq, uint8_t s) {
    uint8_t p[2] = { s, socks[s].sr };
    sendReply(EVT_SOCK_STATE, seq, p, 2);
}

/* ---- socket servicing (never transmits) --------------------------------
 * Refills a socket's RX staging buffer from lwIP and tracks status changes.
 * Called from loop() and again inside a poll so a reply reflects fresh data.
 */
static void service(uint8_t s) {
    Sock &k = socks[s];

    if (k.proto == AIIE_PROTO_TCP) {
        // Accept one client on a listening socket.
        if (k.server && k.server->hasClient()) {
            if (!k.client.connected()) {
                k.client = k.server->available();
                k.sr = W5100_SR_ESTABLISHED;
            } else {
                k.server->available().stop();   // already busy: refuse extra
            }
        }
        // Refill staging when empty.
        if (k.rxLen == 0 && k.client.available() > 0) {
            int n = k.client.available();
            if (n > STAGE) n = STAGE;
            n = k.client.read(k.rx, n);
            if (n > 0) { k.rxLen = (uint16_t)n; k.rxOff = 0; }
        }
        // Peer closed and everything drained -> CLOSE_WAIT (host will CLOSE).
        if (k.sr == W5100_SR_ESTABLISHED && !k.client.connected() &&
            k.rxLen == 0 && k.client.available() == 0) {
            k.client.stop();
            k.sr = W5100_SR_CLOSE_WAIT;
        }
    } else if (k.proto == AIIE_PROTO_UDP) {
        if (k.rxLen == 0) {
            int psize = k.udp.parsePacket();
            if (psize > 0) {
                int n = psize;
                if (n > STAGE) n = STAGE;
                ipToBytes(k.udp.remoteIP(), k.srcIp);
                k.srcPort = k.udp.remotePort();
                n = k.udp.read(k.rx, n);
                if (n > 0) { k.rxLen = (uint16_t)n; k.rxOff = 0; k.hasSrc = true; }
            }
        }
    }
}

/* ---- command handlers -------------------------------------------------- */

static void sendInfo(uint8_t seq) {
    uint8_t p[3 + 1 + 6 + 4];
    uint8_t mac[6];
    WiFi.macAddress(mac);
    p[0] = AIIE_ESP_PROTO_VERSION;
    p[1] = FW_MAJOR;
    p[2] = FW_MINOR;
    p[3] = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    memcpy(p + 4, mac, 6);
    ipToBytes(WiFi.localIP(), p + 10);
    sendReply(EVT_INFO, seq, p, sizeof(p));
}

static void sendWifi(uint8_t seq) {
    uint8_t p[5];
    p[0] = (WiFi.status() == WL_CONNECTED) ? 1 : 0;
    ipToBytes(WiFi.localIP(), p + 1);
    sendReply(EVT_WIFI, seq, p, sizeof(p));
}

static void handleWifiJoin(uint8_t seq, const uint8_t *p, uint16_t len) {
    if (len < 2) { replyAck(seq, AIIE_ESP_ST_EBADARG); return; }
    uint8_t sl = p[0];
    if (1 + sl + 1 > len) { replyAck(seq, AIIE_ESP_ST_EBADARG); return; }
    uint8_t pl = p[1 + sl];
    if (1 + sl + 1 + pl > len) { replyAck(seq, AIIE_ESP_ST_EBADARG); return; }

    char ssid[33], pass[65];
    if (sl > 32) sl = 32;
    if (pl > 64) pl = 64;
    memcpy(ssid, p + 1, sl); ssid[sl] = 0;
    memcpy(pass, p + 1 + sl + 1, pl); pass[pl] = 0;

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);          /* async; the Teensy polls CMD_WIFI_STATUS */
    replyAck(seq, AIIE_ESP_ST_OK);
}

static void handleDnsResolve(uint8_t seq, const uint8_t *p, uint16_t len) {
    char host[128];
    if (len == 0 || len > sizeof(host) - 1) { replyAck(seq, AIIE_ESP_ST_EBADARG); return; }
    memcpy(host, p, len); host[len] = 0;

    dnsDone = false;
    dnsResult = 0;
    ip_addr_t addr;
    err_t e = dns_gethostbyname(host, &addr, dnsCallback, nullptr);
    if (e == ERR_OK) {                  // cached: resolved immediately
        dnsResult = ipaddr_v4(&addr);
        dnsDone = true;
        dnsPending = false;
    } else if (e == ERR_INPROGRESS) {   // callback will fire later
        dnsPending = true;
    } else {                            // outright failure
        dnsPending = false;
        dnsDone = true;
    }
    replyAck(seq, AIIE_ESP_ST_OK);
}

static void handleDnsResult(uint8_t seq) {
    uint8_t p[5];
    p[0] = dnsPending ? 1 : 0;
    p[1] = (uint8_t)(dnsResult & 0xFF);
    p[2] = (uint8_t)((dnsResult >> 8) & 0xFF);
    p[3] = (uint8_t)((dnsResult >> 16) & 0xFF);
    p[4] = (uint8_t)((dnsResult >> 24) & 0xFF);
    sendReply(EVT_DNS, seq, p, 5);
}

static void closeSock(uint8_t s) {
    Sock &k = socks[s];
    k.client.stop();
    if (k.server) { k.server->stop(); delete k.server; k.server = nullptr; }
    if (k.proto == AIIE_PROTO_UDP) k.udp.stop();
    k.proto = 0xFF;
    k.rxLen = 0; k.rxOff = 0; k.hasSrc = false;
    k.sr = W5100_SR_CLOSED;
}

static void handleOpen(uint8_t seq, const uint8_t *p, uint16_t len) {
    if (len < 5) { replyErr(seq, AIIE_ESP_ST_EBADARG); return; }
    uint8_t s      = p[0];
    uint8_t proto  = p[1];
    /* p[2] = ipproto (IP-RAW only) */
    uint16_t lport = (uint16_t)p[3] | ((uint16_t)p[4] << 8);
    if (s >= AIIE_ESP_NUM_SOCKETS) { replyErr(seq, AIIE_ESP_ST_EBADSOCK); return; }

    closeSock(s);
    Sock &k = socks[s];
    k.proto = proto;

    switch (proto) {
    case AIIE_PROTO_TCP:
        k.sr = W5100_SR_INIT;
        replyState(seq, s);
        break;
    case AIIE_PROTO_UDP:
        k.udp.begin(lport);
        k.sr = W5100_SR_UDP;
        replyState(seq, s);
        break;
    default:
        /* IPRAW -> lwIP raw_new(); MACRAW -> NAPT netif. Not in this baseline. */
        k.proto = 0xFF;
        replyErr(seq, AIIE_ESP_ST_ENOTIMPL);
        break;
    }
}

static void handleConnect(uint8_t seq, const uint8_t *p, uint16_t len) {
    if (len < 7) { replyErr(seq, AIIE_ESP_ST_EBADARG); return; }
    uint8_t s = p[0];
    if (s >= AIIE_ESP_NUM_SOCKETS || socks[s].proto != AIIE_PROTO_TCP) {
        replyErr(seq, AIIE_ESP_ST_EBADSOCK); return;
    }
    IPAddress ip(p[1], p[2], p[3], p[4]);
    uint16_t port = (uint16_t)p[5] | ((uint16_t)p[6] << 8);

    /* Bounded blocking connect: this is the one command that can stall the
     * link, briefly, and only when a socket is being opened. The status goes
     * straight to ESTABLISHED or CLOSED; a future non-blocking implementation
     * could instead report SYNSENT and let the Teensy poll for ESTABLISHED. */
    socks[s].client.setTimeout(1000);
    bool ok = socks[s].client.connect(ip, port);
    socks[s].sr = ok ? W5100_SR_ESTABLISHED : W5100_SR_CLOSED;
    replyState(seq, s);
}

static void handleListen(uint8_t seq, const uint8_t *p, uint16_t len) {
    if (len < 3) { replyErr(seq, AIIE_ESP_ST_EBADARG); return; }
    uint8_t s = p[0];
    if (s >= AIIE_ESP_NUM_SOCKETS || socks[s].proto != AIIE_PROTO_TCP) {
        replyErr(seq, AIIE_ESP_ST_EBADSOCK); return;
    }
    uint16_t lport = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
    Sock &k = socks[s];
    if (k.server) { k.server->stop(); delete k.server; }
    k.server = new WiFiServer(lport);
    k.server->begin();
    k.sr = W5100_SR_LISTEN;
    replyState(seq, s);
}

static void handleSend(uint8_t seq, const uint8_t *p, uint16_t len) {
    if (len < 2) { replyErr(seq, AIIE_ESP_ST_EBADARG); return; }
    uint8_t s     = p[0];
    uint8_t flags = p[1];
    if (s >= AIIE_ESP_NUM_SOCKETS) { replyErr(seq, AIIE_ESP_ST_EBADSOCK); return; }
    Sock &k = socks[s];

    const uint8_t *data = p + 2;
    uint16_t dlen = len - 2;
    IPAddress dip; uint16_t dport = 0;
    if (flags & AIIE_SEND_HAS_DEST) {
        if (dlen < 6) { replyErr(seq, AIIE_ESP_ST_EBADARG); return; }
        dip = IPAddress(data[0], data[1], data[2], data[3]);
        dport = (uint16_t)data[4] | ((uint16_t)data[5] << 8);
        data += 6; dlen -= 6;
    }

    uint16_t accepted = 0;
    if (k.proto == AIIE_PROTO_TCP && k.client.connected()) {
        accepted = (uint16_t)k.client.write(data, dlen);
    } else if (k.proto == AIIE_PROTO_UDP && (flags & AIIE_SEND_HAS_DEST)) {
        k.udp.beginPacket(dip, dport);
        k.udp.write(data, dlen);
        if (k.udp.endPacket()) accepted = dlen;
    }

    uint8_t r[3] = { s, (uint8_t)(accepted & 0xFF), (uint8_t)(accepted >> 8) };
    sendReply(EVT_SOCK_SENT, seq, r, 3);
}

static void handlePoll(uint8_t seq, const uint8_t *p, uint16_t len) {
    if (len < 3) { replyErr(seq, AIIE_ESP_ST_EBADARG); return; }
    uint8_t s = p[0];
    uint16_t maxlen = (uint16_t)p[1] | ((uint16_t)p[2] << 8);
    if (s >= AIIE_ESP_NUM_SOCKETS) { replyErr(seq, AIIE_ESP_ST_EBADSOCK); return; }
    Sock &k = socks[s];

    service(s);

    // Header is up to 11 bytes (sock, sr, remain[2], flags, sip[4], sport[2])
    // ahead of up to STAGE data bytes; size with headroom so a full-MTU
    // datagram never overruns this buffer.
    static uint8_t out[16 + STAGE];
    uint16_t n = 0;              /* data bytes to send */
    uint16_t remain = 0;        /* bytes still waiting after this reply */
    uint8_t  flags = 0;
    const uint8_t *src = nullptr;

    if (k.proto == AIIE_PROTO_TCP) {
        uint16_t avail = k.rxLen - k.rxOff;
        n = avail;
        if (n > maxlen)   n = maxlen;
        if (n > RX_CHUNK) n = RX_CHUNK;
        // (data pointer resolved below, before we mutate rxOff)
    } else if (k.proto == AIIE_PROTO_UDP && k.rxLen > 0) {
        if (maxlen >= k.rxLen) {           /* whole datagram fits */
            n = k.rxLen;
            flags = AIIE_DATA_HAS_SRC;
            src = k.srcIp;
        } else {                            /* cannot split a datagram */
            n = 0;
            remain = k.rxLen;               /* hint: needs a bigger window */
        }
    }

    /* assemble: sock, sr, remain(2), flags, [sip(4),sport(2)], data */
    uint16_t o = 0;
    out[o++] = s;
    out[o++] = k.sr;
    uint16_t remainPos = o; o += 2;         /* backfilled below */
    out[o++] = flags;
    if (flags & AIIE_DATA_HAS_SRC) {
        memcpy(out + o, src, 4); o += 4;
        out[o++] = (uint8_t)(k.srcPort & 0xFF);
        out[o++] = (uint8_t)(k.srcPort >> 8);
    }
    if (n) {
        const uint8_t *data = (k.proto == AIIE_PROTO_TCP) ? (k.rx + k.rxOff) : k.rx;
        memcpy(out + o, data, n); o += n;
    }

    /* advance/clear staging, then recompute how much is still waiting */
    if (k.proto == AIIE_PROTO_TCP) {
        k.rxOff += n;
        if (k.rxOff >= k.rxLen) { k.rxLen = 0; k.rxOff = 0; }
        service(s);
        remain = (k.rxLen - k.rxOff) + (uint16_t)k.client.available();
    } else if (k.proto == AIIE_PROTO_UDP) {
        if (n) {                            /* delivered the datagram */
            k.rxLen = 0; k.rxOff = 0; k.hasSrc = false;
            service(s);
            remain = k.rxLen;               /* size of the next datagram, if any */
        }
    }

    out[remainPos]     = (uint8_t)(remain & 0xFF);
    out[remainPos + 1] = (uint8_t)(remain >> 8);
    sendReply(EVT_SOCK_DATA, seq, out, o);
}

/* ---- frame dispatch: every command produces exactly one reply ---------- */
static void onFrame(uint8_t type, uint8_t seq, const uint8_t *p, uint16_t len) {
    // A command carrying the seq we last answered is a retry: resend the
    // cached reply rather than re-executing the command.
    if (s_haveLast && seq == s_lastSeq) {
        if (s_lastFrameLen) LINK.write(s_lastFrame, s_lastFrameLen);
        return;
    }
    switch (type) {
    case CMD_RESET:
        for (uint8_t i = 0; i < AIIE_ESP_NUM_SOCKETS; ++i) closeSock(i);
        replyAck(seq, AIIE_ESP_ST_OK);
        break;
    case CMD_LINK_PING:   sendReply(EVT_LINK_PONG, seq, p, len); break;
    case CMD_GET_INFO:    sendInfo(seq);                     break;
    case CMD_WIFI_JOIN:   handleWifiJoin(seq, p, len);       break;
    case CMD_WIFI_LEAVE:  WiFi.disconnect(); replyAck(seq, AIIE_ESP_ST_OK); break;
    case CMD_WIFI_STATUS: sendWifi(seq);                     break;
    case CMD_DNS_RESOLVE: handleDnsResolve(seq, p, len);     break;
    case CMD_DNS_RESULT:  handleDnsResult(seq);              break;
    case CMD_SOCK_OPEN:   handleOpen(seq, p, len);           break;
    case CMD_SOCK_CONNECT:handleConnect(seq, p, len);        break;
    case CMD_SOCK_LISTEN: handleListen(seq, p, len);         break;
    case CMD_SOCK_SEND:   handleSend(seq, p, len);           break;
    case CMD_SOCK_CLOSE:
        if (len >= 1 && p[0] < AIIE_ESP_NUM_SOCKETS) closeSock(p[0]);
        replyState(seq, (len >= 1 && p[0] < AIIE_ESP_NUM_SOCKETS) ? p[0] : 0);
        break;
    case CMD_SOCK_POLL:   handlePoll(seq, p, len);           break;
    case CMD_RAW_CONFIG:
    case CMD_RAW_SEND:
    case CMD_RAW_POLL:    replyErr(seq, AIIE_ESP_ST_ENOTIMPL); break;  /* TODO */
    default:              replyErr(seq, AIIE_ESP_ST_EBADARG);  break;
    }
}

static FrameParser parser(onFrame);

/* ---- Arduino entry points --------------------------------------------- */
void setup() {
    LINK.begin(LINK_BAUD);
    LINK.setRxBufferSize(1024);         /* absorb bursts; no HW flow control */
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    /* No boot banner: the Teensy discovers us by polling (CMD_GET_INFO /
     * CMD_LINK_PING). Sending anything unsolicited would violate half-duplex. */
}

void loop() {
    /* Feed the parser; each complete command triggers exactly one reply. */
    while (LINK.available()) parser.feed((uint8_t)LINK.read());

    /* Keep RX staging buffers topped up so a poll can answer with fresh data.
     * This does not transmit, so it is safe on a half-duplex link. */
    for (uint8_t s = 0; s < AIIE_ESP_NUM_SOCKETS; ++s) {
        if (socks[s].proto == AIIE_PROTO_TCP || socks[s].proto == AIIE_PROTO_UDP)
            service(s);
    }
}
