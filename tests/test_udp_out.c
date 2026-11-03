/* Unit test of the UDP live stream: exact datagram bytes. */
#include "udp_out.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void) {
    test_format();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("test_udp_out: all checks passed\n");
    return EXIT_SUCCESS;
}
