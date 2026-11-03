#include "udp_out.h"

#include <stdio.h>

int udp_out_format(char *buf, size_t size, double epoch_s, const fnb58_measurement *m) {
    int n = snprintf(buf, size, "{\"epoch_s\":%.3f,\"voltage_v\":%.4f,\"current_a\":%.4f,\"power_w\":%.4f}\n",
                     epoch_s, m->voltage_v, m->current_a, m->power_w);
    return (n < 0 || (size_t) n >= size) ? -1 : n;
}
