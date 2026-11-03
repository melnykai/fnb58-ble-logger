/*
 * udp_out - live stream of measurements as UDP datagrams, one JSON line each.
 *
 * Plain C, no GLib, so it can be unit-tested without the daemon. Sending never
 * blocks and is never retried; failures are only counted. The CSV file is the record.
 */
#ifndef UDP_OUT_H
#define UDP_OUT_H

#include <stddef.h>

#include "fnb58_proto.h"

#define UDP_OUT_MAX_DATAGRAM 128

typedef struct {
    int fd;                       /* -1 when the stream is off */
    unsigned long sent;
    unsigned long errors;         /* failed sends since the last udp_out_take_errors() */
    int last_errno;               /* errno of the most recent failed send */
} udp_out;

/*
 * Writes {"epoch_s":…,"voltage_v":…,"current_a":…,"power_w":…} and a newline into buf,
 * with the CSV's decimals (3 for epoch_s, 4 for the others).
 * Returns the length without the terminating NUL, or -1 if buf is too small.
 */
int udp_out_format(char *buf, size_t size, double epoch_s, const fnb58_measurement *m);

/* Stream off: fd = -1, counters at 0. udp_out_send() on it does nothing. */
void udp_out_init(udp_out *u);

/*
 * Resolves HOST:PORT ([HOST]:PORT for IPv6) and opens a non-blocking UDP socket
 * connected to it, so the kernel reports a refused port on later sends.
 * Returns 0, or -1 with a reason such as "cannot resolve HOST" in err.
 */
int udp_out_open(udp_out *u, const char *host_port, char *err, size_t err_size);

/* Sends one datagram without blocking. A failure only increments u->errors. */
void udp_out_send(udp_out *u, double epoch_s, const fnb58_measurement *m);

/* Returns the failed sends counted since the previous call, and resets the count. */
unsigned long udp_out_take_errors(udp_out *u);

void udp_out_close(udp_out *u);

#endif
