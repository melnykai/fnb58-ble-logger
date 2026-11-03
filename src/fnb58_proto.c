#include "fnb58_proto.h"

#include <string.h>

#define FRAME_START 0xAA

static int known_type(uint8_t type) {
    return type >= 0x04 && type <= 0x08;
}

void fnb58_parser_init(fnb58_parser *p) {
    memset(p, 0, sizeof(*p));
}

static void extract(fnb58_parser *p, fnb58_frame_cb cb, void *user) {
    size_t pos = 0;
    for (;;) {
        while (pos < p->len && p->buf[pos] != FRAME_START) {
            pos++;
            p->dropped_bytes++;
        }
        if (p->len - pos < 3) break;

        uint8_t type = p->buf[pos + 1];
        uint8_t len = p->buf[pos + 2];
        if (!known_type(type) || len > FNB58_MAX_PAYLOAD) {
            // Not a frame start: skip this AA and resync
            pos++;
            p->dropped_bytes++;
            continue;
        }
        size_t total = (size_t) len + FNB58_FRAME_OVERHEAD;
        if (p->len - pos < total) break;

        fnb58_frame f = {
            .type = type,
            .len = len,
            .payload = &p->buf[pos + 3],
            .raw = &p->buf[pos],
            .raw_len = total,
        };
        p->frames++;
        if (cb) cb(&f, user);
        pos += total;
    }
    memmove(p->buf, p->buf + pos, p->len - pos);
    p->len -= pos;
}

void fnb58_parser_feed(fnb58_parser *p, const uint8_t *data, size_t n, fnb58_frame_cb cb, void *user) {
    while (n > 0) {
        size_t room = sizeof(p->buf) - p->len;
        size_t k = n < room ? n : room;
        memcpy(p->buf + p->len, data, k);
        p->len += k;
        data += k;
        n -= k;
        extract(p, cb, user);
        if (p->len == sizeof(p->buf)) {
            // Cannot happen with frames <= 68 bytes, but never wedge on garbage
            p->dropped_bytes += p->len;
            p->len = 0;
        }
    }
}

static int32_t le32(const uint8_t *b) {
    return (int32_t) ((uint32_t) b[0] | (uint32_t) b[1] << 8 | (uint32_t) b[2] << 16 | (uint32_t) b[3] << 24);
}

int fnb58_decode_measurement(const fnb58_frame *frame, fnb58_measurement *m) {
    if (frame->type != FNB58_TYPE_MEASUREMENT || frame->len != 12) return -1;

    double v = le32(frame->payload) / 10000.0;
    double i = le32(frame->payload + 4) / 10000.0;
    double w = le32(frame->payload + 8) / 10000.0;
    if (v < 0.0 || v > 30.0 || i < -10.0 || i > 10.0) return -1;

    m->voltage_v = v;
    m->current_a = i;
    m->power_w = w;
    return 0;
}
