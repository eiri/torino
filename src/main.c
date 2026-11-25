#include "host_link.h"
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pio_usb.h"
#include "tusb.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

atomic_uint link, heartbeat;
atomic_bool requested, cancelled;
probe_t probe;
static uint8_t address, instance, output[64];
#ifndef TORINO_RELAY
static uint8_t transmitted[3][64];
#endif
static uint32_t began;
#ifdef TORINO_CURRENT_READ
static uint32_t current_began, current_elapsed;
#endif
#ifdef TORINO_RELAY
#include "gain.h"
atomic_uint gain_requested, gain_confirmed, capture_index;
atomic_bool gain_live, confirm_requested;
atomic_bool reconnect_ack, write_in_flight, recover_requested, recovery_started;
native_queue_t native_reports, native_events, read_traces;
atomic_uint idle_count, idle_header, settings_reads;
atomic_uint trace_drops;
static uint32_t trace_cycle, trace_command, trace_sent, trace_last, loop_last, loop_peak;
atomic_uint recovery_version, recovery_confirmed;
atomic_bool recovery_confirm_requested;
control_plan_t control_command;
probe_t recovery_capture, selected_capture;
atomic_bool select_read, selected_ready;
static bool reading_selection;
static gain_t gain;
const char *host_error;
static bool write_done, mutation, current_ready, recovering, read_paused, detached, transport_lost;
static unsigned baseline_index, read_index;
static uint32_t gain_epoch, gain_at, stage_at, last_input;
#endif

static probe_t *reader(void) {
#ifdef TORINO_RELAY
    if (read_index) return &recovery_capture;
#endif
    return &probe;
}

static bool reading(void) {
    return reader()->phase >= P_HELLO && reader()->phase <= P_SETTINGS;
}

static void publish(void) {
    probe_t *p = reader();
#ifdef TORINO_RELAY
    if (detached && !atomic_load(&write_in_flight)) {
        atomic_store(&link, L_OFFLINE);
        return;
    }
    if (gain.failed) {
        if (gain.error) host_error = gain.error;
        atomic_store(&link, L_FAILED);
        return;
    }
    if (mutation && !write_done) { atomic_store(&link, L_WRITING); return; }
    if (recovering) {
        if (p->phase == P_DONE) {
            atomic_store(&capture_index, read_index);
            atomic_store(&link, L_RECOVERY_DONE);
        } else {
            atomic_store(&link, p->phase == P_FAILED ? L_FAILED : L_RECOVERING);
        }
        return;
    }
#endif
    if (p->phase == P_FAILED) atomic_store(&link, L_FAILED);
    if (p->phase == P_DONE) {
#ifdef TORINO_RELAY
        atomic_store(&capture_index, read_index);
        atomic_store(&link, write_done ? L_WRITE_DONE : current_ready ? L_CURRENT : L_DONE);
#else
        atomic_store(&link, L_DONE);
#endif
    }
}

void tuh_hid_mount_cb(uint8_t dev, uint8_t idx, const uint8_t *desc, uint16_t len) {
#ifdef TORINO_RELAY
    if (address || atomic_load(&write_in_flight) || (gain.failed && !detached)) return;
#else
    if (probe.phase != P_IDLE) return;
#endif
    uint16_t vid = 0, pid = 0;
    tuh_itf_info_t info;
    if (!tuh_vid_pid_get(dev, &vid, &pid) || !tuh_hid_itf_get_info(dev, idx, &info) ||
        !probe_target(vid, pid, info.desc.bInterfaceNumber, desc, len) ||
        info.desc.bAlternateSetting != 0 || info.desc.bNumEndpoints != 2 ||
        info.desc.bInterfaceProtocol != HID_ITF_PROTOCOL_NONE || address != 0) {
        probe_fail(&probe, "target-descriptor");
        publish();
        return;
    }
    address = dev;
    instance = idx;
#ifdef TORINO_RELAY
    host_tune_polling(dev, vid, pid);
    atomic_fetch_add(&host_sessions, 1);
    if (detached) return;
#endif
    atomic_store(&link, L_READY);
}

void tuh_hid_umount_cb(uint8_t dev, uint8_t idx) {
    if (dev == address && idx == instance) {
#ifdef TORINO_RELAY
        address = instance = 0;
        if (mutation || atomic_load(&gain_requested) != gain_epoch || atomic_load(&write_in_flight)) {
            atomic_store(&write_in_flight, true);
            host_error = "disconnected-during-write";
            gain.failed = true;
        } else if (!detached) {
            detached = true;
            atomic_store(&reconnect_ack, false);
        }
#else
        probe_fail(reader(), "disconnected");
#endif
        publish();
    }
}

void tuh_hid_report_received_cb(uint8_t dev, uint8_t idx, const uint8_t *data, uint16_t len) {
    if (dev != address || idx != instance) return;
#ifdef TORINO_RELAY
    uint32_t received_us = time_us_32();
    bool trace = reader()->phase == P_SETTINGS;
    unsigned report = reader()->count;
    if (len != 64 && !atomic_load(&write_in_flight) && !mutation) {
        transport_lost = true;
        return;
    }
    last_input = to_ms_since_boot(get_absolute_time());
    bool physical = native_notice(data, len) ||
        (len == 64 && data[0] == 0x1c && data[1] == 1 && !reading());
    bool marker = len == 64 && data[0] == 0x1d && data[1] == 1;
    if (physical || marker || (reader()->phase == P_DONE && (!mutation || write_done))) {
        atomic_fetch_add(&idle_count, 1);
        atomic_store(&idle_header, (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
                                  (uint32_t)data[2] << 8 | data[3]);
        (void)native_push(&native_reports, data, len);
        if (physical && !reading() && atomic_load(&gain_live) &&
            !native_push(&native_events, data, len)) {
            host_error = "physical-queue-overflow";
            gain.failed = true;
        }
        if (!tuh_hid_receive_report(dev, idx)) gain.failed = true;
        publish();
        return;
    }
    if (mutation && !write_done) {
        if (gain_receive(&gain, data, len) && !tuh_hid_receive_report(dev, idx)) gain.failed = true;
        publish();
        return;
    }
#endif
    if (!reading()) return;
    probe_receive(reader(), data, len);
#ifdef TORINO_RELAY
    if (reading_selection && !atomic_load(&selected_ready) &&
        probe_selected(&selected_capture, reader(), baseline_index ? &recovery_capture : &probe))
        atomic_store(&selected_ready, true);
#endif
#ifdef TORINO_CURRENT_READ
    if (!reading()) current_elapsed = time_us_32() - current_began;
#endif
    bool receive = reading();
#ifdef TORINO_RELAY
    receive = receive || reader()->phase == P_DONE;
    if (recovering && reader()->phase == P_DONE) {
        read_paused = true;
        receive = false;
    }
#endif
    if (receive && !tuh_hid_receive_report(dev, idx)) probe_fail(reader(), "receive-submit");
#ifdef TORINO_RELAY
    if (trace) {
        read_trace_t event = {
            .cycle = trace_cycle, .report = report, .command_us = trace_command,
            .sent_us = trace_sent, .received_us = received_us,
            .rearm_us = receive ? time_us_32() - received_us : UINT32_MAX,
            .gap_us = trace_last ? received_us - trace_last : 0,
            .loop_us = loop_peak, .poll = atomic_load(&endpoint_timing),
        };
        trace_last = received_us;
        loop_peak = 0;
        if (!native_push(&read_traces, (const uint8_t *)&event, sizeof(event)))
            atomic_fetch_add(&trace_drops, 1);
    }
#endif
    publish();
}

void tuh_hid_report_sent_cb(uint8_t dev, uint8_t idx, const uint8_t *data, uint16_t len) {
    if (dev != address || idx != instance) return;
#ifdef TORINO_RELAY
    if (len != 64 && !atomic_load(&write_in_flight) && !mutation) {
        transport_lost = true;
        return;
    }
    if (reading() && reader()->phase == P_SETTINGS && len == 64 &&
        data[0] == 0xff && data[1] == 0xc1) trace_sent = time_us_32();
    if (gain.pending) {
        gain_sent(&gain, data, len);
        publish();
        return;
    }
#endif
    if (!reading()) return;
    if (len != 64 || memcmp(data, output, 64) != 0) {
        probe_fail(reader(), "output-mismatch");
    } else {
        probe_sent(reader(), len);
    }
    publish();
}

#ifdef TORINO_RELAY
static void reset_session(void) {
    probe = (probe_t){0};
    recovery_capture = (probe_t){0};
    gain = (gain_t){0};
    host_error = NULL;
    write_done = mutation = current_ready = recovering = read_paused = false;
    baseline_index = read_index = 0;
    reading_selection = false;
    atomic_store(&select_read, false);
    atomic_store(&selected_ready, false);
    gain_epoch = gain_at = stage_at = last_input = 0;
    atomic_store(&requested, false);
    atomic_store(&cancelled, false);
    atomic_store(&gain_live, false);
    atomic_store(&gain_requested, 0);
    atomic_store(&gain_confirmed, 0);
    atomic_store(&confirm_requested, false);
    atomic_store(&capture_index, 0);
    atomic_store(&recovery_version, 0);
    atomic_store(&recovery_confirmed, 0);
    atomic_store(&recovery_confirm_requested, false);
    detached = transport_lost = false;
    atomic_store(&link, address ? L_READY : L_WAIT);
}
#endif

void host_main(void) {
    pio_usb_configuration_t config = PIO_USB_DEFAULT_CONFIG;
    config.pin_dp = 12;
    config.pinout = PIO_USB_PINOUT_DPDM;
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_FULL};
    if (!tuh_configure(1, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &config) ||
        !tuh_rhport_init(1, &init)) {
        probe_fail(&probe, "host-init");
        publish();
    }
    for (;;) {
#ifdef TORINO_RELAY
        uint32_t loop_now = time_us_32();
        uint32_t gap = loop_last ? loop_now - loop_last : 0;
        if (gap > loop_peak) loop_peak = gap;
        loop_last = loop_now;
#endif
        tuh_task_ext(0, false);
        uint32_t now = to_ms_since_boot(get_absolute_time());
        atomic_store(&heartbeat, now);
#ifdef TORINO_RELAY
        if (reading() && !atomic_load(&write_in_flight) &&
            now - began >= 2000 && now - last_input >= 2000) transport_lost = true;
        if (transport_lost && !detached && !mutation && !atomic_load(&write_in_flight)) {
            detached = true;
            atomic_store(&reconnect_ack, false);
            host_forget();
            publish();
        }
        host_discover(now, !address && !detached && !gain.failed && probe.phase == P_IDLE &&
            !atomic_load(&write_in_flight));
        if (detached) {
            if (atomic_load(&write_in_flight)) {
                gain.failed = true;
                detached = false;
                publish();
            } else if (atomic_exchange(&reconnect_ack, false)) {
                reset_session();
            }
            continue;
        }
#endif
        if (atomic_load(&requested) && atomic_load(&link) == L_READY && probe_start(&probe)) {
            began = now;
            atomic_store(&link, L_READING);
            if (!tuh_hid_receive_report(address, instance)) probe_fail(&probe, "receive-start");
        }
#ifdef TORINO_RELAY
        if (atomic_exchange(&recover_requested, false)) {
            reading_selection = atomic_exchange(&select_read, false);
            gain = (gain_t){0};
            mutation = write_done = current_ready = false;
            recovering = true;
            read_paused = false;
            gain_epoch = atomic_load(&gain_requested);
            atomic_store(&confirm_requested, false);
            atomic_store(&recovery_confirm_requested, false);
            atomic_store(&cancelled, false);
            atomic_fetch_add(&recovery_version, 1);
            read_index = 1 - baseline_index;
            *reader() = (probe_t){0};
            began = last_input = now;
            if (!address || !probe_refresh(reader(), baseline_index ? &recovery_capture : &probe)) {
                gain.failed = true;
                host_error = "recovery-read";
            } else if (tuh_hid_receive_ready(address, instance) &&
                       !tuh_hid_receive_report(address, instance)) {
                gain.failed = true;
                host_error = "recovery-receive";
            }
            publish();
            atomic_store(&recovery_started, true);
        }
        if (atomic_load(&cancelled) && atomic_load(&requested)) {
            if (!host_error) host_error = "cancelled";
            gain.failed = true;
        }
        if (atomic_exchange(&recovery_confirm_requested, false)) {
            if (gain.failed || !recovering ||
                atomic_load(&recovery_confirmed) != atomic_load(&recovery_version) || reader()->phase != P_DONE || !read_paused) {
                gain.failed = true;
            } else {
                baseline_index = read_index;
                recovering = read_paused = reading_selection = false;
                current_ready = true;
                if (!tuh_hid_receive_report(address, instance)) gain.failed = true;
            }
        }
        if (atomic_exchange(&confirm_requested, false)) {
            if (gain.failed || recovering || !atomic_load(&gain_live) || atomic_load(&gain_confirmed) != gain_epoch ||
                reader()->phase != P_DONE || (gain_epoch && !write_done)) {
                gain.failed = true;
            } else {
                baseline_index = read_index;
                mutation = write_done = false;
                current_ready = true;
                gain = (gain_t){0};
            }
        }
        if (!gain.failed && mutation && gain.completed == gain_steps(&gain) && !write_done) {
            write_done = true;
        }
        if (gain.pending && now - stage_at >= 2000) {
            host_error = "gain-stage-timeout";
            gain.failed = true;
        }
        uint32_t request = atomic_load(&gain_requested);
        if (request != gain_epoch && !gain.failed) {
            if (mutation || recovering || !atomic_load(&gain_live) ||
                request != gain_epoch + 1 || reader()->phase != P_DONE) {
                host_error = "gain-request-state";
                gain.failed = true;
            } else {
                gain_at = now;
                gain_epoch = request;
                bool started = control_command.module == CONTROL_MODULES + 2 ?
                    gain_save(&gain, control_command.expected[4], (const char *)control_command.expected + 16) :
                    control_command.module == CONTROL_MODULES + 3 ?
                    gain_expression(&gain, control_command.expected[16]) :
                    control_command.module == CONTROL_MODULES + 1 ?
                    gain_tuner(&gain, control_command.expected[0] != 0) :
                    control_command.module == CONTROL_MODULES ?
                    gain_select(&gain, control_command.expected[4]) :
                    gain_batch(&gain, (const uint8_t (*)[64])control_command.packets, control_command.count);
                mutation = true;
                current_ready = false;
                if (!started) gain.failed = true;
            }
        }
        if (mutation && (!write_done || reader()->phase != P_DONE) && now - gain_at >= 30000) {
            host_error = "gain-cycle-timeout";
            gain.failed = true;
        }
        if (!gain.failed && mutation && gain_next(&gain, output)) {
            stage_at = now;
            if (!tuh_hid_send_report(address, instance, 0, output, sizeof(output))) {
                host_error = "gain-send-submit";
                gain.failed = true;
            }
        }
#endif
        if (reading() && atomic_load(&cancelled)) probe_fail(reader(), "serial-closed");
        if (reading() && now - began >= 15000) probe_fail(reader(), "read-deadline");
        if (
#ifdef TORINO_RELAY
            !gain.failed &&
#endif
            probe_next(reader(), output)) {
#ifndef TORINO_RELAY
            if (reader() == &probe) memcpy(transmitted[probe.writes - 1], output, sizeof(output));
#endif
#ifdef TORINO_CURRENT_READ
            if (reader()->phase == P_SETTINGS) current_began = time_us_32();
#endif
#ifdef TORINO_RELAY
            if (reader()->phase == P_SETTINGS) {
                trace_cycle++;
                atomic_fetch_add(&settings_reads, 1);
                trace_command = time_us_32();
                trace_sent = trace_last = loop_peak = 0;
            }
#endif
            if (!tuh_hid_send_report(address, instance, 0, output, sizeof(output))) {
                probe_fail(reader(), "send-submit");
            }
        }
        publish();
        tight_loop_contents();
    }
}

void feed_watchdog(void) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - atomic_load(&heartbeat) < 1000) watchdog_update();
}

#ifndef TORINO_RELAY
static bool serial_text(const char *text, size_t length) {
    uint32_t began = to_ms_since_boot(get_absolute_time());
    while (length) {
        tud_task_ext(0, false);
        feed_watchdog();
        if (!tud_cdc_connected() || to_ms_since_boot(get_absolute_time()) - began >= 1000) return false;
        uint32_t sent = tud_cdc_write(text, length);
        text += sent;
        length -= sent;
        tud_cdc_write_flush();
    }
    return true;
}

static bool dump_packet(const char *kind, const uint8_t *packet) {
    static const char hex[] = "0123456789abcdef";
    char line[224];
    size_t n = (size_t)snprintf(line, sizeof(line), "%s report:", kind);
    for (size_t i = 0; i < 64; i++) {
        line[n++] = ' ';
        line[n++] = hex[packet[i] >> 4];
        line[n++] = hex[packet[i] & 15];
    }
    line[n++] = '\n';
    return serial_text(line, n);
}

static void dump_result(void) {
    for (size_t i = 0; i < probe.count || i < probe.writes; i++) {
        if (i < probe.writes && !dump_packet("TX", transmitted[i])) return;
        if (i < probe.count && !dump_packet("RX", probe.reports[i])) return;
    }
    char line[128];
    int n;
#ifdef TORINO_CURRENT_READ
    n = snprintf(line, sizeof(line), "CURRENT flags=c5 settings_us=%lu records=%u\n",
                 (unsigned long)current_elapsed, probe.count);
    if (!serial_text(line, (size_t)n)) return;
#endif
    if (probe.phase == P_DONE) {
        n = snprintf(line, sizeof(line), "DONE settings=146 writes=3\n");
    } else {
        n = snprintf(line, sizeof(line), "ERROR %s records=%u writes=%u\n",
                     probe.error ? probe.error : "unknown", probe.count, probe.writes);
    }
    serial_text(line, (size_t)n);
}

int main(void) {
    set_sys_clock_khz(120000, true);
    watchdog_enable(5000, true);
    multicore_launch_core1(host_main);
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tud_rhport_init(0, &init)) for (;;) tight_loop_contents();

    char command[8];
    size_t used = 0;
    uint32_t last_banner = 0;
    bool dumped = false;
    for (;;) {
        tud_task_ext(0, false);
        feed_watchdog();
        if (!tud_cdc_connected()) {
            used = 0;
            if (atomic_load(&requested)) atomic_store(&cancelled, true);
            continue;
        }
        unsigned state = atomic_load(&link);
        if ((state == L_DONE || state == L_FAILED) && !dumped) {
            dumped = true;
            dump_result();
        }
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (!dumped && now - last_banner >= 1000) {
            last_banner = now;
            const char *line = state == L_READY ? "STATE READY\n" :
                               state == L_READING ? "STATE READING\n" : "STATE WAITING\n";
            serial_text(line, strlen(line));
        }
        while (tud_cdc_available()) {
            char c;
            if (tud_cdc_read(&c, 1) != 1) break;
            if (c == '\r') continue;
            if (c == '\n') {
                bool valid = used == 4 && memcmp(command, "READ", 4) == 0;
                if (!valid || state != L_READY || atomic_exchange(&requested, true)) {
                    const char *line = "ERROR command-or-state\n";
                    serial_text(line, strlen(line));
                }
                used = 0;
            } else if (used < sizeof(command)) {
                command[used++] = c;
            } else {
                used = sizeof(command);
            }
        }
    }
}
#endif
