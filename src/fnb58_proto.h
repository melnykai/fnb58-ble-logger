/*
 * FNIRSI FNB58 Bluetooth LE stream protocol (observed on one FNB58 unit, 2026-11-02).
 *
 * GATT   write  0000ffe9-... (service 0000ffe5-...)  start commands aa8100f4, aa8200a7
 *        notify 0000ffe4-... (service 0000ffe0-...)
 *
 * Notifications carry concatenated frames:  AA <type> <len> <payload:len> <check>
 * The check byte is deterministic but matches no CRC-8/sum/xor we could identify,
 * so frames are validated by structure (known type, sane length) and value range.
 *
 *   type 0x04, len 12: int32 LE voltage, current, power; each / 10000 -> V, A, W
 *   types 0x05..0x08 : not decoded yet (kept raw)
 *
 * Plain C, no GLib: unit-testable on its own.
 */
#ifndef FNB58_PROTO_H
#define FNB58_PROTO_H

#include <stddef.h>
#include <stdint.h>

#define FNB58_MAX_PAYLOAD 64
#define FNB58_FRAME_OVERHEAD 4 /* AA, type, len, check */
#define FNB58_TYPE_MEASUREMENT 0x04

typedef struct {
    uint8_t type;
    uint8_t len;
    const uint8_t *payload; /* points into raw, valid only during the callback */
    const uint8_t *raw;     /* whole frame, AA .. check */
    size_t raw_len;
} fnb58_frame;

typedef struct {
    double voltage_v;
    double current_a;
    double power_w;
} fnb58_measurement;

typedef void (*fnb58_frame_cb)(const fnb58_frame *frame, void *user);

typedef struct {
    uint8_t buf[256];
    size_t len;
    unsigned long frames;
    unsigned long dropped_bytes;
} fnb58_parser;

void fnb58_parser_init(fnb58_parser *p);

/* Append bytes from one notification; calls cb once per complete frame.
 * Frames may span notifications: incomplete tails are kept for the next call. */
void fnb58_parser_feed(fnb58_parser *p, const uint8_t *data, size_t n, fnb58_frame_cb cb, void *user);

/* Decode a type 0x04 frame. Returns 0 on success, -1 if not a measurement or implausible. */
int fnb58_decode_measurement(const fnb58_frame *frame, fnb58_measurement *m);

#endif
