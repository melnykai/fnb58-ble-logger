#define _GNU_SOURCE /* SOCK_NONBLOCK, SOCK_CLOEXEC, MSG_NOSIGNAL */
#include "udp_out.h"

#include <errno.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int udp_out_format(char *buf, size_t size, double epoch_s, const fnb58_measurement *m) {
    int n = snprintf(buf, size, "{\"epoch_s\":%.3f,\"voltage_v\":%.4f,\"current_a\":%.4f,\"power_w\":%.4f}\n",
                     epoch_s, m->voltage_v, m->current_a, m->power_w);
    return (n < 0 || (size_t) n >= size) ? -1 : n;
}

void udp_out_init(udp_out *u) {
    memset(u, 0, sizeof(*u));
    u->fd = -1;
}

/* Splits "host:port" or "[v6]:port" into host and port; 0 on success. */
static int split_host_port(const char *s, char *host, size_t host_size, char *port, size_t port_size) {
    const char *colon;
    size_t host_len;
    if (s[0] == '[') {
        const char *end = strchr(s, ']');
        if (end == NULL || end[1] != ':') return -1;
        host_len = (size_t) (end - s - 1);
        memcpy(host, s + 1, host_len < host_size ? host_len : 0);
        colon = end + 1;
    } else {
        colon = strrchr(s, ':');
        if (colon == NULL || memchr(s, ':', (size_t) (colon - s)) != NULL) return -1; /* bare IPv6 */
        host_len = (size_t) (colon - s);
        memcpy(host, s, host_len < host_size ? host_len : 0);
    }
    if (host_len == 0 || host_len >= host_size) return -1;
    host[host_len] = '\0';

    char *end;
    long p = strtol(colon + 1, &end, 10);
    if (colon[1] == '\0' || *end != '\0' || p < 1 || p > 65535) return -1;
    snprintf(port, port_size, "%ld", p);
    return 0;
}

int udp_out_open(udp_out *u, const char *host_port, char *err, size_t err_size) {
    char host[256], port[8];
    udp_out_init(u);
    if (split_host_port(host_port, host, sizeof(host), port, sizeof(port)) != 0) {
        snprintf(err, err_size, "bad address %s, expected HOST:PORT", host_port);
        return -1;
    }

    struct addrinfo hints = {.ai_family = AF_UNSPEC, .ai_socktype = SOCK_DGRAM}, *res;
    int rc = getaddrinfo(host, port, &hints, &res);
    if (rc != 0) {
        snprintf(err, err_size, "cannot resolve %s (%s)", host, gai_strerror(rc));
        return -1;
    }
    int last = 0;
    for (struct addrinfo *ai = res; ai != NULL && u->fd < 0; ai = ai->ai_next) {
        int fd = socket(ai->ai_family, ai->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, ai->ai_protocol);
        if (fd < 0) {
            last = errno;
        } else if (connect(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
            last = errno;
            close(fd);
        } else {
            u->fd = fd;
        }
    }
    freeaddrinfo(res);
    if (u->fd < 0) {
        snprintf(err, err_size, "cannot reach %s: %s", host_port, strerror(last));
        return -1;
    }
    return 0;
}

void udp_out_send(udp_out *u, double epoch_s, const fnb58_measurement *m) {
    if (u->fd < 0) return;
    char buf[UDP_OUT_MAX_DATAGRAM];
    int n = udp_out_format(buf, sizeof(buf), epoch_s, m);
    if (n > 0 && send(u->fd, buf, (size_t) n, MSG_DONTWAIT | MSG_NOSIGNAL) == n) {
        u->sent++;
    } else {
        u->errors++;
        u->last_errno = n > 0 ? errno : EMSGSIZE;
    }
}

unsigned long udp_out_take_errors(udp_out *u) {
    unsigned long n = u->errors;
    u->errors = 0;
    return n;
}

void udp_out_close(udp_out *u) {
    if (u->fd >= 0) close(u->fd);
    u->fd = -1;
}
