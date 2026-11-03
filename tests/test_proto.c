/* Unit test of the frame parser on notifications captured from a real FNB58 (2026-11-02). */
#include "fnb58_proto.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *CAPTURE[] = {
    "aa060610004600010be3aa07042b146e00d1aa040c9dc900001e040000421500008caa0507f67707000133014f",
    "aa060610004600010be3aa07042b146b0024",
    "aa060610004600010be3aa070428146800abaa040c9cc900001e0400004115000025aa05074778070001320168",
    "aa060610004600010be3aa0704281469009aaa0811015c330000b8080000d7020000b9010000a1",
    "aa060610004600010be3aa07042d146600e1",
};

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static size_t unhex(const char *s, uint8_t *out) {
    size_t n = strlen(s) / 2;
    for (size_t k = 0; k < n; k++) {
        unsigned v;
        sscanf(s + 2 * k, "%2x", &v);
        out[k] = (uint8_t) v;
    }
    return n;
}

typedef struct {
    int by_type[256];
    int measurements;
    fnb58_measurement first;
} Tally;

static void on_frame(const fnb58_frame *f, void *user) {
    Tally *t = user;
    t->by_type[f->type]++;
    fnb58_measurement m;
    if (fnb58_decode_measurement(f, &m) == 0) {
        if (t->measurements++ == 0) t->first = m;
    }
}

/* Feed the whole capture, cutting the byte stream into chunks of `chunk` bytes. */
static void run(size_t chunk, const char *garbage_prefix, Tally *t) {
    uint8_t stream[1024];
    size_t n = 0;
    if (garbage_prefix) n += unhex(garbage_prefix, stream);
    for (size_t k = 0; k < sizeof(CAPTURE) / sizeof(*CAPTURE); k++) n += unhex(CAPTURE[k], stream + n);

    fnb58_parser p;
    fnb58_parser_init(&p);
    memset(t, 0, sizeof(*t));
    for (size_t off = 0; off < n; off += chunk) {
        size_t k = n - off < chunk ? n - off : chunk;
        fnb58_parser_feed(&p, stream + off, k, on_frame, t);
    }
    CHECK(p.len == 0);
}

static void expect_capture(const Tally *t) {
    CHECK(t->by_type[0x04] == 2);
    CHECK(t->by_type[0x05] == 2);
    CHECK(t->by_type[0x06] == 5);
    CHECK(t->by_type[0x07] == 5);
    CHECK(t->by_type[0x08] == 1);
    CHECK(t->measurements == 2);
    CHECK(fabs(t->first.voltage_v - 5.1613) < 1e-9);
    CHECK(fabs(t->first.current_a - 0.1054) < 1e-9);
    CHECK(fabs(t->first.power_w - 0.5442) < 1e-9);
}

int main(void) {
    Tally t;

    // Whole notifications, then every chunk size down to single bytes (frames split anywhere)
    for (size_t chunk = 1; chunk <= 100; chunk++) {
        run(chunk, NULL, &t);
        expect_capture(&t);
    }

    // Leading garbage, including a false AA start, is skipped
    run(20, "0102aa99ffaa", &t);
    expect_capture(&t);

    // Implausible voltage is rejected
    uint8_t bad[16];
    unhex("aa040cffffff7f1e040000421500008c", bad); // INT32_MAX / 10000 V
    fnb58_frame f = {.type = bad[1], .len = bad[2], .payload = bad + 3, .raw = bad, .raw_len = 16};
    fnb58_measurement m;
    CHECK(fnb58_decode_measurement(&f, &m) == -1);

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("test_proto: all checks passed\n");
    return EXIT_SUCCESS;
}
