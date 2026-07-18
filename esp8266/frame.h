/* UART framing for the aiie <-> ESP protocol.  See protocol.h / PROTOCOL.md. */
#ifndef AIIE_ESP_FRAME_H
#define AIIE_ESP_FRAME_H

#include <Arduino.h>
#include "protocol.h"

/* Called once per validated (CRC-checked) frame.  `payload` points into the
 * parser's internal buffer and is only valid for the duration of the call. */
typedef void (*FrameHandler)(uint8_t type, uint8_t seq,
                             const uint8_t *payload, uint16_t len);

class FrameParser {
public:
    explicit FrameParser(FrameHandler h) : handler(h) { reset(); }
    void reset();
    void feed(uint8_t b);          /* push one received byte                 */

    /* diagnostics */
    uint32_t crcErrors = 0;
    uint32_t framesOk  = 0;

private:
    enum State : uint8_t { HUNT0, HUNT1, LEN0, LEN1, BODY };
    State        st;
    FrameHandler handler;
    uint16_t     bodyLen;          /* TYPE+SEQ+PAYLOAD length (the LEN field) */
    uint16_t     idx;
    /* holds TYPE+SEQ+PAYLOAD then the 2 CRC bytes */
    uint8_t      buf[AIIE_ESP_MAX_PAYLOAD + 2 + 2];
};

/* Assemble one frame (type/seq/payload) into `out` and return its total length
 * (0 if the payload is too big). `out` must hold at least AIIE_ESP_MAX_FRAME
 * bytes. Used to cache a reply for retransmit as well as to send it. */
uint16_t frameBuild(uint8_t *out, uint8_t type, uint8_t seq,
                    const uint8_t *payload, uint16_t len);

/* Assemble one frame (type/seq/payload) and write it to `out` in a single
 * buffered write.  `len` is the payload length and must be <= MAX_PAYLOAD. */
void frameSend(Stream &out, uint8_t type, uint8_t seq,
               const uint8_t *payload, uint16_t len);

/* Convenience: a frame with no payload. */
static inline void frameSend0(Stream &out, uint8_t type, uint8_t seq) {
    frameSend(out, type, seq, nullptr, 0);
}

#endif /* AIIE_ESP_FRAME_H */
