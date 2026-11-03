/*
 * udp_out - live stream of measurements as UDP datagrams, one JSON line each.
 *
 * Plain C, no GLib, so it can be unit-tested without the daemon.
 */
#ifndef UDP_OUT_H
#define UDP_OUT_H

#include <stddef.h>

#include "fnb58_proto.h"

#define UDP_OUT_MAX_DATAGRAM 128

/*
 * Writes {"epoch_s":…,"voltage_v":…,"current_a":…,"power_w":…} and a newline into buf,
 * with the CSV's decimals (3 for epoch_s, 4 for the others).
 * Returns the length without the terminating NUL, or -1 if buf is too small.
 */
int udp_out_format(char *buf, size_t size, double epoch_s, const fnb58_measurement *m);

#endif
