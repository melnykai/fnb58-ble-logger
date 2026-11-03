/* Unit test of the UDP live stream: datagram bytes, addresses, and sending to a closed port. */
#define _GNU_SOURCE
#include "udp_out.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static int failures;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static void test_format(void) {
    char buf[UDP_OUT_MAX_DATAGRAM];

    // The scenario of the spec: same values and decimals as the CSV line
    fnb58_measurement m = {.voltage_v = 5.1613, .current_a = 0.1054, .power_w = 0.5442};
    const char *want = "{\"epoch_s\":1793699888.642,\"voltage_v\":5.1613,\"current_a\":0.1054,\"power_w\":0.5442}\n";
    int n = udp_out_format(buf, sizeof(buf), 1793699888.642, &m);
    CHECK(n == (int) strlen(want));
    CHECK(strcmp(buf, want) == 0);

    // Negative current (reverse flow) and the largest plausible values still fit
    fnb58_measurement big = {.voltage_v = 30.0, .current_a = -10.0, .power_w = -300.0};
    n = udp_out_format(buf, sizeof(buf), 4102444800.0, &big);
    CHECK(n > 0);
    CHECK(strcmp(buf, "{\"epoch_s\":4102444800.000,\"voltage_v\":30.0000,\"current_a\":-10.0000,\"power_w\":-300.0000}\n") == 0);

    // A buffer that is too small is refused, not truncated
    CHECK(udp_out_format(buf, 20, 1793699888.642, &m) == -1);
}

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;
}

/* A local UDP port that nothing listens on: bind an ephemeral port, then release it. */
static int closed_port(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t len = sizeof(a);
    bind(fd, (struct sockaddr *) &a, sizeof(a));
    getsockname(fd, (struct sockaddr *) &a, &len);
    close(fd);
    return ntohs(a.sin_port);
}

static void test_addresses(void) {
    udp_out u;
    char err[256];

    // Bad syntax and unknown hosts are refused with a reason
    const char *bad[] = {"localhost", "localhost:", "localhost:0", "localhost:70000", "localhost:12x",
                         ":5005", "::1:5005", "[::1]5005"};
    for (size_t k = 0; k < sizeof(bad) / sizeof(*bad); k++) {
        CHECK(udp_out_open(&u, bad[k], err, sizeof(err)) == -1);
        CHECK(strncmp(err, "bad address ", 12) == 0);
        CHECK(u.fd == -1);
    }
    CHECK(udp_out_open(&u, "nowhere.invalid:9", err, sizeof(err)) == -1);
    CHECK(strncmp(err, "cannot resolve nowhere.invalid", 30) == 0);

    // IPv4 and bracketed IPv6 both open
    CHECK(udp_out_open(&u, "127.0.0.1:5005", err, sizeof(err)) == 0);
    udp_out_close(&u);
    if (udp_out_open(&u, "[::1]:5005", err, sizeof(err)) == 0) udp_out_close(&u);
    else printf("note: IPv6 loopback unavailable here (%s), skipped\n", err);
}

static void test_send(void) {
    udp_out u;
    char err[256], addr[32];
    fnb58_measurement m = {.voltage_v = 5.1613, .current_a = 0.1054, .power_w = 0.5442};

    // Stream off: nothing is sent, nothing is counted
    udp_out_init(&u);
    udp_out_send(&u, 1.0, &m);
    CHECK(u.sent == 0 && u.errors == 0);

    // Received intact by a listener
    int rx = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t len = sizeof(a);
    bind(rx, (struct sockaddr *) &a, sizeof(a));
    getsockname(rx, (struct sockaddr *) &a, &len);
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", ntohs(a.sin_port));
    CHECK(udp_out_open(&u, addr, err, sizeof(err)) == 0);
    udp_out_send(&u, 1793699888.642, &m);
    char got[UDP_OUT_MAX_DATAGRAM] = {0}, want[UDP_OUT_MAX_DATAGRAM];
    struct timeval tv = {.tv_sec = 1};
    setsockopt(rx, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    ssize_t n = recv(rx, got, sizeof(got) - 1, 0);
    udp_out_format(want, sizeof(want), 1793699888.642, &m);
    CHECK(n == (ssize_t) strlen(want) && strcmp(got, want) == 0);
    CHECK(u.sent == 1 && u.errors == 0);
    udp_out_close(&u);
    close(rx);

    // Closed port: every call returns at once, refusals are counted, never retried
    snprintf(addr, sizeof(addr), "127.0.0.1:%d", closed_port());
    CHECK(udp_out_open(&u, addr, err, sizeof(err)) == 0);
    double slowest = 0;
    for (int k = 0; k < 20; k++) {
        double t0 = now_s();
        udp_out_send(&u, 1.0, &m);
        double dt = now_s() - t0;
        if (dt > slowest) slowest = dt;
        usleep(2000); /* let the ICMP port-unreachable come back */
    }
    CHECK(slowest < 0.05);
    CHECK(u.errors >= 5);
    CHECK(u.sent + u.errors == 20);
    CHECK(u.last_errno == ECONNREFUSED);
    unsigned long taken = udp_out_take_errors(&u);
    CHECK(taken >= 5 && u.errors == 0);
    printf("closed port: %lu of 20 sends refused, slowest call %.3f ms\n", taken, slowest * 1000);
    udp_out_close(&u);
}

int main(void) {
    test_format();
    test_addresses();
    test_send();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("test_udp_out: all checks passed\n");
    return EXIT_SUCCESS;
}
