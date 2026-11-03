/*
 * fnb58d - logs an FNIRSI FNB58 USB tester over Bluetooth LE to daily CSV files.
 *
 * One GLib main loop drives bluez_inc (BlueZ over D-Bus):
 *   scan (name or address prefix) -> connect -> services resolved
 *   -> write aa8100f4, then aa8200a7 -> start notify -> frames -> CSV
 * Lost link, no data for --watchdog seconds, or a stuck connect -> drop the device and
 * rescan with backoff 1, 2, 4 ... 30 s. Measurements go to CSV only; stderr carries
 * events (journald under systemd, with <N> priority prefixes).
 */
#include <errno.h>
#include <glib.h>
#include <glib-unix.h>
#include <glib/gstdio.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "adapter.h"
#include "device.h"
#include "logger.h"

#include "fnb58_proto.h"
#include "udp_out.h"

#define WRITE_SERVICE "0000ffe5-0000-1000-8000-00805f9b34fb"
#define WRITE_CHAR "0000ffe9-0000-1000-8000-00805f9b34fb"
#define NOTIFY_SERVICE "0000ffe0-0000-1000-8000-00805f9b34fb"
#define NOTIFY_CHAR "0000ffe4-0000-1000-8000-00805f9b34fb"

#define CONNECT_TIMEOUT_S 30
#define DISCONNECT_TIMEOUT_S 10
#define SCAN_RESTART_S 60
#define MAX_BACKOFF_S 30
#define STATS_PERIOD_S 600

static const guint8 START_COMMANDS[][4] = {
    {0xaa, 0x81, 0x00, 0xf4},
    {0xaa, 0x82, 0x00, 0xa7},
};
#define N_START_COMMANDS (sizeof(START_COMMANDS) / sizeof(*START_COMMANDS))

typedef enum {
    ST_IDLE, ST_SCANNING, ST_CONNECTING, ST_CONNECTED, ST_STREAMING, ST_DISCONNECTING
} State;

static const char *STATE_NAMES[] = {"idle", "scanning", "connecting", "connected", "streaming", "disconnecting"};

/* ---- options ---------------------------------------------------------------------- */

static gchar *opt_address = NULL;
static gchar *opt_name = NULL;
static gchar *opt_dir = NULL;
static gint opt_watchdog = 10;
static gint opt_keep_days = 14;
static gboolean opt_raw = FALSE;
static gboolean opt_verbose = FALSE;
static gchar *opt_udp = NULL;

static GOptionEntry OPTIONS[] = {
    {"address", 'a', 0, G_OPTION_ARG_STRING, &opt_address, "Tester BLE address (default: match by name)", "MAC"},
    {"name", 'n', 0, G_OPTION_ARG_STRING, &opt_name, "Device name prefix to scan for (default FNB58)", "NAME"},
    {"dir", 'o', 0, G_OPTION_ARG_FILENAME, &opt_dir, "Directory for the daily CSV files (default .)", "DIR"},
    {"watchdog", 'w', 0, G_OPTION_ARG_INT, &opt_watchdog, "Reconnect after N s without data (default 10)", "N"},
    {"keep-days", 'k', 0, G_OPTION_ARG_INT, &opt_keep_days, "Delete CSV files older than N days, 0 = keep all (default 14)", "N"},
    {"raw", 'r', 0, G_OPTION_ARG_NONE, &opt_raw, "Also log every raw frame to <day>_fnb58_raw.csv", NULL},
    {"verbose", 'v', 0, G_OPTION_ARG_NONE, &opt_verbose, "Debug messages, including bluez_inc", NULL},
    {"udp", 'u', 0, G_OPTION_ARG_STRING, &opt_udp, "Also send each measurement as a JSON datagram (default off)", "HOST:PORT"},
    {NULL, 0, 0, 0, NULL, NULL, NULL},
};

/* ---- application state ------------------------------------------------------------ */

static struct {
    GMainLoop *loop;
    GDBusConnection *dbus;
    Adapter *adapter;
    Device *device;
    State state;
    gint64 state_since;   /* monotonic us */
    gint64 last_rx;       /* monotonic us of the last measurement */
    gint64 last_stats;
    guint backoff_s;
    guint rescan_id;
    gsize init_step;
    fnb58_parser parser;
    gboolean stopping;

    char day[11];         /* YYYY-MM-DD of the open files */
    FILE *csv;
    FILE *raw;
    udp_out udp;

    unsigned long measurements;
    unsigned long connects;
    fnb58_measurement last;
} app;

/* ---- event log: stderr, with syslog priority prefixes when journald reads it ------ */

static gboolean to_journal;

static void say(int prio, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

static void say(int prio, const char *fmt, ...) {
    if (prio == 7 && !opt_verbose) return;
    va_list ap;
    va_start(ap, fmt);
    if (to_journal) {
        fprintf(stderr, "<%d>", prio);
    } else {
        GDateTime *now = g_date_time_new_now_local();
        gchar *stamp = g_date_time_format(now, "%H:%M:%S");
        fprintf(stderr, "%s ", stamp);
        g_free(stamp);
        g_date_time_unref(now);
    }
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

#define LOG_ERR(...) say(3, __VA_ARGS__)
#define LOG_WARN(...) say(4, __VA_ARGS__)
#define LOG_INFO(...) say(6, __VA_ARGS__)
#define LOG_DBG(...) say(7, __VA_ARGS__)

static void on_binc_log(LogLevel level, const char *tag, const char *message) {
    static const int prio[] = {7, 6, 4, 3};
    say(prio[level], "[binc %s] %s", tag, message);
}

static void set_state(State s) {
    if (s != app.state) LOG_DBG("state %s -> %s", STATE_NAMES[app.state], STATE_NAMES[s]);
    app.state = s;
    app.state_since = g_get_monotonic_time();
}

/* ---- CSV files -------------------------------------------------------------------- */

static FILE *open_csv(const char *day, const char *suffix, const char *header) {
    gchar *path = g_strdup_printf("%s/%s_fnb58%s.csv", opt_dir, day, suffix);
    FILE *f = fopen(path, "a");
    if (f == NULL) {
        LOG_ERR("cannot open %s: %s", path, g_strerror(errno));
    } else {
        if (ftell(f) == 0) fprintf(f, "%s\n", header);
        LOG_INFO("logging to %s", path);
    }
    g_free(path);
    return f;
}

static void close_files(void) {
    if (app.csv) fclose(app.csv);
    if (app.raw) fclose(app.raw);
    app.csv = app.raw = NULL;
    app.day[0] = '\0';
}

/* Delete <YYYY-MM-DD>_fnb58*.csv older than --keep-days (ISO dates compare as strings). */
static void purge_old_files(void) {
    if (opt_keep_days <= 0) return;

    time_t cutoff_t = time(NULL) - (time_t) opt_keep_days * 86400;
    struct tm tm;
    char cutoff[11];
    localtime_r(&cutoff_t, &tm);
    strftime(cutoff, sizeof(cutoff), "%Y-%m-%d", &tm);

    GDir *dir = g_dir_open(opt_dir, 0, NULL);
    if (dir == NULL) return;
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        if (strlen(name) < 16 || strncmp(name + 10, "_fnb58", 6) != 0 || !g_str_has_suffix(name, ".csv")) continue;
        if (strncmp(name, cutoff, 10) >= 0) continue;
        gchar *path = g_build_filename(opt_dir, name, NULL);
        if (g_unlink(path) == 0) LOG_INFO("deleted %s (older than %d days)", path, opt_keep_days);
        else LOG_WARN("cannot delete %s: %s", path, g_strerror(errno));
        g_free(path);
    }
    g_dir_close(dir);
}

/* Fills "2026-11-03T10:15:02.331+0100" and epoch seconds; switches files at midnight. */
static gboolean prepare_files(char *stamp, size_t stamp_size, double *epoch) {
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);

    char day[11], hms[20], tz[8];
    strftime(day, sizeof(day), "%Y-%m-%d", &tm);
    strftime(hms, sizeof(hms), "%H:%M:%S", &tm);
    strftime(tz, sizeof(tz), "%z", &tm);
    snprintf(stamp, stamp_size, "%sT%s.%03d%s", day, hms, (int) (ts.tv_nsec / 1000000) % 1000, tz);
    *epoch = (double) ts.tv_sec + (double) ts.tv_nsec / 1e9;

    if (strcmp(day, app.day) != 0) {
        close_files();
        app.csv = open_csv(day, "", "time_iso,epoch_s,voltage_v,current_a,power_w");
        if (opt_raw) app.raw = open_csv(day, "_raw", "time_iso,epoch_s,type,frame_hex");
        g_strlcpy(app.day, day, sizeof(app.day));
        purge_old_files();
    }
    return app.csv != NULL;
}

static void on_frame(const fnb58_frame *frame, void *user) {
    char stamp[64];
    double epoch;
    if (!prepare_files(stamp, sizeof(stamp), &epoch)) return;

    if (app.raw) {
        fprintf(app.raw, "%s,%.3f,%02x,", stamp, epoch, frame->type);
        for (size_t k = 0; k < frame->raw_len; k++) fprintf(app.raw, "%02x", frame->raw[k]);
        fputc('\n', app.raw);
    }

    fnb58_measurement m;
    if (fnb58_decode_measurement(frame, &m) != 0) return;

    fprintf(app.csv, "%s,%.3f,%.4f,%.4f,%.4f\n", stamp, epoch, m.voltage_v, m.current_a, m.power_w);
    udp_out_send(&app.udp, epoch, &m); /* after the CSV line, never blocks */
    app.measurements++;
    app.last = m;
    app.last_rx = g_get_monotonic_time();

    if (app.state != ST_STREAMING) {
        set_state(ST_STREAMING);
        app.backoff_s = 1;
        LOG_INFO("streaming: %.4f V  %.4f A  %.4f W", m.voltage_v, m.current_a, m.power_w);
    }
}

/* ---- BLE life cycle --------------------------------------------------------------- */

static void start_scan(void);

static gboolean on_rescan(gpointer data) {
    app.rescan_id = 0;
    start_scan();
    return G_SOURCE_REMOVE;
}

static void schedule_rescan(void) {
    if (app.stopping || app.rescan_id != 0) return;
    LOG_INFO("rescanning in %u s", app.backoff_s);
    app.rescan_id = g_timeout_add_seconds(app.backoff_s, on_rescan, NULL);
    app.backoff_s = MIN(app.backoff_s * 2, MAX_BACKOFF_S);
}

/* Forget the device (BlueZ RemoveDevice cancels any pending connect) and start over. */
static void link_lost(void) {
    if (app.device != NULL) {
        Device *device = app.device;
        app.device = NULL;
        binc_adapter_remove_device(app.adapter, device);
    }
    fnb58_parser_init(&app.parser);
    set_state(ST_IDLE);
    schedule_rescan();
}

static void force_disconnect(const char *why) {
    LOG_WARN("%s, dropping the link", why);
    if (app.device != NULL && binc_device_get_connection_state(app.device) == BINC_CONNECTED) {
        set_state(ST_DISCONNECTING);
        binc_device_disconnect(app.device);
    } else {
        link_lost();
    }
}

static void send_start_command(gsize step) {
    app.init_step = step;
    GByteArray *bytes = g_byte_array_new();
    g_byte_array_append(bytes, START_COMMANDS[step], sizeof(START_COMMANDS[step]));
    gboolean ok = binc_device_write_char(app.device, WRITE_SERVICE, WRITE_CHAR, bytes, WITH_RESPONSE);
    g_byte_array_free(bytes, TRUE);
    if (!ok) force_disconnect("write characteristic ffe9 not found (not an FNB58?)");
}

static void on_write(Device *device, Characteristic *characteristic, const GByteArray *bytes, const GError *error) {
    if (device != app.device) return;
    if (error != NULL) {
        LOG_WARN("start command %zu failed: %s", app.init_step + 1, error->message);
        force_disconnect("start sequence failed");
        return;
    }
    if (app.init_step + 1 < N_START_COMMANDS) {
        send_start_command(app.init_step + 1);
    } else if (!binc_device_start_notify(device, NOTIFY_SERVICE, NOTIFY_CHAR)) {
        force_disconnect("notify characteristic ffe4 not found (not an FNB58?)");
    }
}

static void on_notify_state(Device *device, Characteristic *characteristic, const GError *error) {
    if (device != app.device) return;
    if (error != NULL) {
        LOG_WARN("enabling notifications failed: %s", error->message);
        force_disconnect("no notifications");
        return;
    }
    LOG_DBG("notifications %s", binc_characteristic_is_notifying(characteristic) ? "on" : "off");
}

static void on_notify(Device *device, Characteristic *characteristic, const GByteArray *bytes) {
    if (device != app.device || bytes == NULL) return;
    fnb58_parser_feed(&app.parser, bytes->data, bytes->len, on_frame, NULL);
}

static void on_services_resolved(Device *device) {
    if (device != app.device) return;
    LOG_DBG("services resolved, sending start commands");
    send_start_command(0);
}

static void on_connection_state(Device *device, ConnectionState state, const GError *error) {
    if (device != app.device) return;
    if (error != NULL) LOG_WARN("connection error: %s", error->message);

    if (state == BINC_CONNECTED) {
        app.connects++;
        set_state(ST_CONNECTED);
        LOG_INFO("connected to %s (%s)", binc_device_get_name(device), binc_device_get_address(device));
    } else if (state == BINC_DISCONNECTED) {
        LOG_WARN("disconnected from %s", binc_device_get_address(device));
        link_lost();
    }
}

static void on_scan_result(Adapter *adapter, Device *device) {
    if (app.state != ST_SCANNING) return;
    LOG_INFO("found %s (%s) rssi %d", binc_device_get_name(device), binc_device_get_address(device),
             binc_device_get_rssi(device));

    binc_adapter_stop_discovery(adapter);
    app.device = device;
    fnb58_parser_init(&app.parser);
    binc_device_set_connection_state_change_cb(device, on_connection_state);
    binc_device_set_services_resolved_cb(device, on_services_resolved);
    binc_device_set_write_char_cb(device, on_write);
    binc_device_set_notify_state_cb(device, on_notify_state);
    binc_device_set_notify_char_cb(device, on_notify);
    set_state(ST_CONNECTING);
    binc_device_connect(device);
}

static void on_discovery_state(Adapter *adapter, DiscoveryState state, const GError *error) {
    if (error == NULL) return;
    LOG_WARN("discovery error: %s", error->message);
    if (app.state == ST_SCANNING) {
        set_state(ST_IDLE);
        schedule_rescan();
    }
}

static void on_powered(Adapter *adapter, gboolean powered) {
    LOG_INFO("adapter %s powered %s", binc_adapter_get_name(adapter), powered ? "on" : "off");
    if (powered) {
        if (app.state == ST_IDLE) start_scan();
    } else {
        app.device = NULL; /* BlueZ drops its devices with the adapter */
        set_state(ST_IDLE);
    }
}

static void start_scan(void) {
    if (app.stopping) return;
    if (!binc_adapter_get_powered_state(app.adapter)) {
        LOG_INFO("powering on adapter %s", binc_adapter_get_name(app.adapter));
        set_state(ST_IDLE);
        binc_adapter_power_on(app.adapter); /* on_powered() starts the scan */
        return;
    }
    const char *pattern = opt_address != NULL ? opt_address : opt_name;
    LOG_INFO("scanning for %s", pattern);
    binc_adapter_set_discovery_filter(app.adapter, -100, NULL, pattern);
    set_state(ST_SCANNING);
    binc_adapter_start_discovery(app.adapter);
}

/* ---- 1 s housekeeping: flush, watchdogs, periodic stats --------------------------- */

static gboolean on_tick(gpointer data) {
    if (app.csv) fflush(app.csv);
    if (app.raw) fflush(app.raw);

    gint64 now = g_get_monotonic_time();
    gint64 in_state = (now - app.state_since) / G_USEC_PER_SEC;

    switch (app.state) {
        case ST_CONNECTING:
        case ST_CONNECTED:
            if (in_state >= CONNECT_TIMEOUT_S) force_disconnect("no data 30 s after connecting");
            break;
        case ST_STREAMING:
            if ((now - app.last_rx) / G_USEC_PER_SEC >= opt_watchdog) {
                char why[64];
                g_snprintf(why, sizeof(why), "no data for %d s", opt_watchdog);
                force_disconnect(why);
            }
            break;
        case ST_DISCONNECTING:
            if (in_state >= DISCONNECT_TIMEOUT_S) link_lost();
            break;
        case ST_SCANNING:
            if (in_state >= SCAN_RESTART_S) {
                LOG_INFO("tester not seen for %d s, restarting discovery", SCAN_RESTART_S);
                binc_adapter_stop_discovery(app.adapter);
                set_state(ST_IDLE);
                schedule_rescan();
            }
            break;
        case ST_IDLE:
            break;
    }

    if ((now - app.last_stats) / G_USEC_PER_SEC >= STATS_PERIOD_S) {
        app.last_stats = now;
        LOG_INFO("stats: %lu measurements, %lu connects, %lu bytes dropped; last %.4f V %.4f A %.4f W",
                 app.measurements, app.connects, app.parser.dropped_bytes,
                 app.last.voltage_v, app.last.current_a, app.last.power_w);
    }
    return G_SOURCE_CONTINUE;
}

/* ---- shutdown --------------------------------------------------------------------- */

static gboolean on_quit_timeout(gpointer data) {
    g_main_loop_quit(app.loop);
    return G_SOURCE_REMOVE;
}

static gboolean on_signal(gpointer data) {
    if (app.stopping) return G_SOURCE_CONTINUE;
    LOG_INFO("stopping (%s)", (const char *) data);
    app.stopping = TRUE;
    if (app.device != NULL && binc_device_get_connection_state(app.device) == BINC_CONNECTED) {
        binc_device_disconnect(app.device);
    } else if (app.state == ST_SCANNING) {
        binc_adapter_stop_discovery(app.adapter);
    }
    g_timeout_add(500, on_quit_timeout, NULL);
    return G_SOURCE_CONTINUE;
}

int main(int argc, char **argv) {
    GError *error = NULL;
    GOptionContext *context = g_option_context_new("- log an FNIRSI FNB58 over Bluetooth LE to CSV");
    g_option_context_add_main_entries(context, OPTIONS, NULL);
    if (!g_option_context_parse(context, &argc, &argv, &error)) {
        fprintf(stderr, "%s\n", error->message);
        return 2;
    }
    g_option_context_free(context);

    if (opt_name == NULL) opt_name = g_strdup("FNB58");
    if (opt_dir == NULL) opt_dir = g_strdup(".");
    if (opt_address != NULL) {
        gchar *upper = g_ascii_strup(opt_address, -1); /* BlueZ reports upper-case addresses */
        g_free(opt_address);
        opt_address = upper;
    }
    if (opt_watchdog < 2) opt_watchdog = 2;

    to_journal = !isatty(STDERR_FILENO);
    setvbuf(stderr, NULL, _IOLBF, 0);

    log_set_handler(on_binc_log);
    log_set_level(opt_verbose ? LOG_DEBUG : LOG_WARN);
    log_enabled(TRUE);

    udp_out_init(&app.udp);
    if (opt_udp != NULL) {
        char err[300];
        if (udp_out_open(&app.udp, opt_udp, err, sizeof(err)) != 0) {
            LOG_ERR("udp: %s", err);
            return 2;
        }
    }

    if (g_mkdir_with_parents(opt_dir, 0755) != 0) {
        LOG_ERR("cannot create %s: %s", opt_dir, g_strerror(errno));
        return 1;
    }

    app.dbus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);
    if (app.dbus == NULL) {
        LOG_ERR("no system D-Bus: %s", error->message);
        return 1;
    }
    app.adapter = binc_adapter_get_default(app.dbus);
    if (app.adapter == NULL) {
        LOG_ERR("no Bluetooth adapter found (is bluetoothd running?)");
        return 1;
    }
    LOG_INFO("fnb58d started: adapter %s, CSV dir %s, watchdog %d s, keep %d days%s%s%s",
             binc_adapter_get_name(app.adapter), opt_dir, opt_watchdog, opt_keep_days, opt_raw ? ", raw frames" : "",
             opt_udp ? ", UDP to " : "", opt_udp ? opt_udp : "");

    binc_adapter_set_discovery_cb(app.adapter, on_scan_result);
    binc_adapter_set_discovery_state_cb(app.adapter, on_discovery_state);
    binc_adapter_set_powered_state_cb(app.adapter, on_powered);

    app.loop = g_main_loop_new(NULL, FALSE);
    app.backoff_s = 1;
    app.last_stats = g_get_monotonic_time();
    g_unix_signal_add(SIGINT, on_signal, (gpointer) "SIGINT");
    g_unix_signal_add(SIGTERM, on_signal, (gpointer) "SIGTERM");
    g_timeout_add_seconds(1, on_tick, NULL);

    start_scan();
    g_main_loop_run(app.loop);

    LOG_INFO("stopped: %lu measurements, %lu connects", app.measurements, app.connects);
    close_files();
    udp_out_close(&app.udp);
    binc_adapter_free(app.adapter);
    g_dbus_connection_close_sync(app.dbus, NULL, NULL);
    g_object_unref(app.dbus);
    g_main_loop_unref(app.loop);
    return 0;
}
