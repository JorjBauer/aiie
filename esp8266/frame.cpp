#include "frame.h"
#include <string.h>

void FrameParser::reset() {
    st = HUNT0;
    bodyLen = 0;
    idx = 0;
}

void FrameParser::feed(uint8_t b) {
    switch (st) {
    case HUNT0:
        if (b == AIIE_ESP_SOF0) st = HUNT1;
        break;

    case HUNT1:
        /* Allow a run of SOF0 bytes before SOF1 (helps resync). */
        if (b == AIIE_ESP_SOF1)      st = LEN0;
        else if (b == AIIE_ESP_SOF0) st = HUNT1;
        else                         st = HUNT0;
        break;

    case LEN0:
        bodyLen = b;
        st = LEN1;
        break;

    case LEN1:
        bodyLen |= (uint16_t)b << 8;
        /* bodyLen counts TYPE+SEQ+PAYLOAD; must be >=2 (type+seq) and fit. */
        if (bodyLen < 2 || bodyLen > (AIIE_ESP_MAX_PAYLOAD + 2)) {
            st = HUNT0;                 /* implausible length: resync */
        } else {
            idx = 0;
            st = BODY;
        }
        break;

    case BODY:
        buf[idx++] = b;
        if (idx == (uint16_t)(bodyLen + 2)) {   /* body + 2 CRC bytes */
            const uint16_t got = (uint16_t)buf[bodyLen] |
                                 ((uint16_t)buf[bodyLen + 1] << 8);
            const uint16_t calc = aiie_crc16(buf, bodyLen);
            if (got == calc) {
                framesOk++;
                if (handler)
                    handler(buf[0], buf[1], buf + 2, (uint16_t)(bodyLen - 2));
            } else {
                crcErrors++;
            }
            st = HUNT0;
        }
        break;
    }
}

uint16_t frameBuild(uint8_t *out, uint8_t type, uint8_t seq,
                    const uint8_t *payload, uint16_t len) {
    if (len > AIIE_ESP_MAX_PAYLOAD) return 0;  /* refuse oversize */

    const uint16_t body = (uint16_t)(len + 2); /* type + seq + payload */
    out[0] = AIIE_ESP_SOF0;
    out[1] = AIIE_ESP_SOF1;
    out[2] = (uint8_t)(body & 0xFF);
    out[3] = (uint8_t)(body >> 8);
    out[4] = type;
    out[5] = seq;
    if (len && payload) memcpy(out + 6, payload, len);

    const uint16_t crc = aiie_crc16(out + 4, body);  /* over type+seq+payload */
    out[6 + len]     = (uint8_t)(crc & 0xFF);
    out[6 + len + 1] = (uint8_t)(crc >> 8);

    return (uint16_t)(6 + len + 2);
}

void frameSend(Stream &out, uint8_t type, uint8_t seq,
               const uint8_t *payload, uint16_t len) {
    static uint8_t tx[AIIE_ESP_MAX_FRAME];
    const uint16_t n = frameBuild(tx, type, seq, payload, len);
    if (n) out.write(tx, n);
}
