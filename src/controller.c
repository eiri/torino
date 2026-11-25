#include "host_link.h"
#include "tone.h"
#include "trace.h"
#include "store.h"
#ifndef TORINO_CONTROLLER_TEST
#include "hardware/clocks.h"
#include "hardware/watchdog.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include "device/dcd.h"
#endif
#include <stdio.h>
#include <string.h>

static unsigned epoch, version, next_module, physical_updates;
static control_state_t controls, actual;
static bool pending[CONTROL_MODULES], recovering;
static control_event_t physical;
static control_plan_t edit_plan;
static control_request_t last_control;
static const char *control_result = "-";
static uint8_t slot, target;
static char names[24][33];
static unsigned load_slot;
static bool selecting, clearing, tuner_pending, tuning, tuner_enabled;
static bool selected_early;
static control_event_t selection_event;
static control_state_t selection_state;
static char selection_name[33];
static bool adopted, failed, confirming, operation, refresh, submitted;
static bool usb_paused, offline_seen, new_session;
static uint32_t usb_at;
static uint32_t operation_at, sent_at, physical_at;
static atomic_bool reset_needed;
static uint8_t in_report[64];
static const char *fault = "-";
static bool store_access, save_pending, save_active, expr_pending, expr_active;
static unsigned save_slot;
static char save_name[20];
static uint8_t expression, wanted_expression;

static const probe_t *capture(void) {
    return atomic_load(&capture_index) ? &recovery_capture : &probe;
}

static bool snapshot(const probe_t *p, unsigned *index) {
    if (p->phase != P_DONE || p->pending || p->count != PROBE_REPORTS || p->writes != 3) return false;
    unsigned matches = 0;
    for (unsigned i = 2; i < p->count; i++) {
        const uint8_t *a = p->reports[i];
        if (memcmp(a, "\34\1\5\0", 4)) continue;
        if (a[4] >= 24 || a[17]) return false;
        *index = i; matches++;
    }
    if (matches != 1 || *index < 3 || *index + 6 >= p->count) return false;
    const uint8_t *amp = p->reports[*index];
    unsigned name = 2u + 2u * amp[4];
    for (unsigned i = 2; i < p->count; i++) {
        bool current = (i >= *index - 1 && i <= *index + 6) || i == name || i == name + 1;
        if (!current) continue;
        const uint8_t *a = p->reports[i];
        if (a[0] != 0x1c || a[1] != 1 || a[3] || a[4] != amp[4] ||
            a[6] != 1 || a[7] > 1 || a[7] != amp[7]) return false;
    }
    return true;
}

static void stop_at(const char *reason) {
    if (!failed) {
        fault = reason;
        if (operation && !refresh) control_result = "uncertain";
    }
    failed = true;
    atomic_store(&cancelled, true);
    operation = false;
}

void amp_stop(void) { stop_at("tone-overflow"); }

static bool amp_ready(void) {
    return adopted && !failed && !confirming && !operation && !refresh && atomic_load(&link) == L_CURRENT;
}

bool amp_connected(void) {
    unsigned state = atomic_load(&link);
    return adopted && !failed && state != L_OFFLINE && state != L_WAIT;
}
void amp_snapshot(control_state_t *state) { *state = controls; }

size_t amp_nodes(char *json, size_t size) { return controls_json(&controls, json, size); }
size_t amp_values(const char *node, control_request_t values[CONTROL_VALUES]) {
    return controls_values(&controls, node, values);
}
bool amp_dirty(void) { return controls.valid && controls.raw[0][7]; }
bool amp_path(const char *path[7]) { return controls_path(&controls, path); }
unsigned amp_slot(void) { return slot + 1u; }
const char *amp_name(unsigned slot) {
    return slot >= 1 && slot <= 24 && names[slot - 1][0] ? names[slot - 1] : NULL;
}

bool amp_tuning(void) { return tuner_pending || tuning; }
bool amp_tuner(bool enabled) {
    if ((!store_access && store_busy()) || !amp_connected() || amp_loading() || recovering || amp_tuning()) return false;
    tuner_enabled = enabled;
    tuner_pending = true;
    return true;
}

bool amp_loading(void) {
    return (!store_access && store_busy()) || ((load_slot || selecting) && !selected_early);
}
bool amp_select(unsigned wanted) {
    if (!amp_connected() || amp_loading() || selecting || recovering || wanted < 1 || wanted > 24) return false;
    load_slot = wanted;
    return true;
}

bool amp_apply(const control_state_t *state) {
    if (!amp_connected() || amp_loading() || recovering || !state->valid || state->raw[0][4] != slot) return false;

    bool dirty = controls.raw[0][7] || state->raw[0][7] || memcmp(controls.raw, state->raw, sizeof(controls.raw));
    controls = *state;
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        if (dirty) controls.raw[m][7] = 1;
        pending[m] = true;
    }
    clearing = true;
    return true;
}

bool amp_control(const control_request_t *request) {
    control_plan_t plan;
    last_control = *request;
    if (!amp_connected() || amp_loading()) { control_result = "disconnected"; return false; }
    if (!controls_plan(&controls, request, &plan)) { control_result = "unsupported"; return false; }
    controls_accept(&controls, &plan);
    pending[plan.module] = true;
    if (plan.gate_update) {
        pending[0] = true;
        next_module = plan.gate.selected ? 1 : 0;
    }
    control_result = "accepted";
    return true;
}

static bool read_actual(const probe_t *p) {
    unsigned index = 0;
    if (!snapshot(p, &index) || !controls_adopt(&actual, p)) return false;

    memset(names, 0, sizeof(names));
    for (unsigned s = 0; s < 24; s++) {
        const uint8_t *r = p->reports[2 + 2 * s];
        if (memcmp(r, "\34\1\4\0", 4) || r[4] != s) continue;
        memcpy(names[s], r + 16, 32);
    }
    return true;
}

bool store_online(void) { return amp_connected(); }
unsigned store_slot(void) { return amp_slot(); }
void store_state(control_state_t *state) { *state = controls; }
void store_fault(void) { stop_at("storage-uncertain"); }

bool store_idle(void) {
    if (!amp_ready() || load_slot || selecting || tuner_pending || tuning ||
        save_pending || save_active || expr_pending || expr_active || physical.mask || physical.preset) return false;
    for (unsigned m = 0; m < CONTROL_MODULES; m++) if (pending[m]) return false;
    return true;
}

bool store_get(store_preset_t *preset) {
    if (!store_idle()) return false;
    memset(preset, 0, sizeof(*preset));
    const uint8_t tags[] = {4, 5, 6, 7, 8, 9, 13, 0};
    for (unsigned i = 0; i < 8; i++) {
        uint8_t *r = preset->raw[i];
        r[0] = 0x1c; r[1] = 1; r[2] = tags[i]; r[4] = slot; r[6] = 1;
    }
    for (unsigned m = 0; m < CONTROL_MODULES; m++) memcpy(preset->raw[m + 1], actual.raw[m], 64);
    memcpy(preset->raw[0] + 16, names[slot], 32);
    preset->raw[6][16] = expression;
    return true;
}

bool store_load(unsigned wanted) {
    store_access = true;
    bool ok = amp_select(wanted);
    store_access = false;
    return ok;
}

bool store_restore(const control_state_t *state, uint8_t extra) {
    static control_state_t draft;
    draft = *state;
    for (unsigned m = 0; m < CONTROL_MODULES; m++) draft.raw[m][4] = draft.latent[m][4] = slot;
    store_access = true;
    bool ok = amp_apply(&draft);
    store_access = false;
    if (ok && expression != extra) { wanted_expression = extra; expr_pending = true; }
    return ok;
}

bool store_apply(const store_preset_t *preset) {
    static control_state_t draft;
    draft = controls;
    draft.gate = (control_gate_t){0};
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        memcpy(draft.raw[m], preset->raw[m + 1], 64);
        memcpy(draft.latent[m], draft.raw[m], 64);
    }
    return store_restore(&draft, preset->raw[6][16]);
}

bool store_save(unsigned wanted, const char *name) {
    if (!store_idle() || wanted < 1 || wanted > TORINO_STORE_LIMIT || strlen(name) > 19) return false;
    save_slot = wanted;
    memset(save_name, 0, sizeof(save_name));
    memcpy(save_name, name, strlen(name));
    save_pending = true;
    return true;
}

static void pause_usb(uint32_t now) {
    if (!usb_paused) {
        tud_disconnect();
        usb_paused = true;
        usb_at = now;
    }
    submitted = false;
    tone_reset();
}

static void reset_tone(void) {
    submitted = false;
    tone_reset();
}

static void host_tick(uint32_t now) {
    unsigned state = atomic_load(&link);
    if (state == L_OFFLINE) {
        if (!offline_seen) {
            pause_usb(now);
            adopted = failed = confirming = operation = refresh = false;
            recovering = selecting = clearing = tuner_pending = tuning = false;
            load_slot = 0;
            selected_early = save_pending = save_active = expr_pending = expr_active = false;
            atomic_store(&selected_ready, false);
            memset(pending, 0, sizeof(pending));
            physical = (control_event_t){0};
            uint8_t discard[64];
            while (native_pop(&native_events, discard)) {}
            epoch = version = physical_updates = 0;
            controls = actual = (control_state_t){0};
            memset(names, 0, sizeof(names));
            fault = "-";
            offline_seen = true;
            atomic_store(&reconnect_ack, true);
        }
        return;
    }
    offline_seen = false;
    if (failed) return;
    if (state == L_FAILED) {
        if (store_busy()) { stop_at("storage-failed"); return; }
        if (tuning) { stop_at("tuner-failed"); return; }
        if (selecting) { stop_at("preset-select-failed"); return; }
        if (recovering && (!atomic_load(&recovery_started) || atomic_load(&link) != L_FAILED)) return;
        if (!adopted || (!operation && !recovering && !atomic_load(&write_in_flight))) {
            stop_at("host-failed"); return;
        }
        if (recovering) { stop_at("recovery-failed"); return; }
        recovering = true;
        physical = (control_event_t){0};
        operation = false; confirming = false; refresh = true;
        operation_at = now;
        atomic_store(&recovery_started, false);
        atomic_store(&recover_requested, true);
        return;
    }
    if (state == L_READY && !atomic_load(&requested)) atomic_store(&requested, true);
    if (state == L_DONE && !adopted) {
        if (!read_actual(capture())) { stop_at("startup-snapshot"); return; }
        controls = actual;
        slot = controls.raw[0][4];
        expression = capture()->reports[actual.index[0] + 5][16];
        adopted = confirming = new_session = true;
        operation_at = now;
        atomic_store(&gain_live, true);
        atomic_store(&gain_confirmed, 0);
        atomic_store(&confirm_requested, true);
    }
    if (state == L_WRITE_DONE && selecting && !refresh && !confirming) {
        uint8_t packet[64];
        bool bad = false;
        while (native_pop(&native_events, packet)) {
            if (!selection_event.preset) {
                if (packet[0] != 0x1c || packet[1] != 1 || packet[2] != 4 ||
                    packet[4] + 1u != load_slot) continue;
                memcpy(selection_name, packet + 16, 32); selection_name[32] = 0;
            }
            bool complete = packet[2] == 0 && selection_event.preset &&
                selection_event.mask == (1u << CONTROL_MODULES) - 1;
            if (packet[2] == 13) expression = packet[16];
            unsigned changed;
            if (controls_event(&selection_state, &selection_event, packet, 64, &changed) == E_BAD) {
                bad = true; break;
            }
            if (!complete) continue;
            controls = actual = selection_state;
            slot = controls.raw[0][4];
            memcpy(names[slot], selection_name, sizeof(selection_name));
            memset(pending, 0, sizeof(pending));
            physical = (control_event_t){0};
            selected_early = confirming = true;
            atomic_store(&gain_confirmed, epoch);
            atomic_store(&confirm_requested, true);
            return;
        }
        if (!bad && now - operation_at < 250) return;

        operation = false;
        refresh = true;
        operation_at = now;
        atomic_store(&select_read, true);
        atomic_store(&recover_requested, true);
    }
    if (selecting && !selected_early && atomic_load(&selected_ready)) {
        actual = (control_state_t){0};
        if (!read_actual(&selected_capture) || actual.raw[0][4] + 1u != load_slot) {
            stop_at("preset-early-snapshot"); return;
        }
        controls = actual;
        slot = actual.raw[0][4];
        memset(pending, 0, sizeof(pending));
        physical = (control_event_t){0};
        selected_early = true;
    }
    if (state == L_WRITE_DONE && !confirming && !selecting) {
        if (!operation) { stop_at("completion-state"); return; }
        if (!tuning && !save_active && !expr_active) controls_accept(&actual, &edit_plan);
        if (expr_active) expression = wanted_expression;
        control_result = "completed";
        confirming = true;
        atomic_store(&gain_confirmed, epoch);
        atomic_store(&confirm_requested, true);
    }
    if (state == L_RECOVERY_DONE && !confirming) {
        if (selecting) actual = (control_state_t){0};
        if (!read_actual(capture())) { stop_at("refresh-snapshot"); return; }
        expression = capture()->reports[actual.index[0] + 5][16];
        version = atomic_load(&recovery_version);
        if (selecting) {
            if (actual.raw[0][4] + 1u != load_slot) { stop_at("preset-select-mismatch"); return; }
            if (!selected_early) {
                controls = actual;
                memset(pending, 0, sizeof(pending));
            }
            slot = actual.raw[0][4];
            clearing = false;
            physical = (control_event_t){0};
            uint8_t discard[64];
            while (native_pop(&native_events, discard)) {}
        } else {
            if (!recovering) { stop_at("unexpected-settings-read"); return; }
            if (actual.raw[0][4] != slot) { stop_at("recovery-preset-changed"); return; }
            for (unsigned m = 0; m < CONTROL_MODULES; m++) pending[m] = true;
        }
        confirming = true;
        operation_at = now;
        atomic_store(&recovery_confirmed, version);
        atomic_store(&recovery_confirm_requested, true);
    }
    if (state == L_CURRENT && confirming) {
        confirming = operation = refresh = tuning = save_active = expr_active = false;
        if (selecting) {
            selecting = selected_early = false; load_slot = 0;
            atomic_store(&selected_ready, false);
        }
        atomic_store(&write_in_flight, false);
        if (new_session) { pause_usb(now); new_session = false; }
    }
    if ((operation || confirming || refresh) && now - operation_at >= 30000) {
        stop_at("operation-timeout"); return;
    }
    if (!amp_ready()) return;
    uint8_t packet[64];
    while (native_pop(&native_events, packet)) {
        if (recovering) continue;
        if (packet[0] == 0x1c && packet[1] == 1 && packet[2] == 13) expression = packet[16];
        unsigned changed = 0;
        unsigned previous_slot = slot;
        bool assembling = physical.mask || physical.preset;
        enum control_event result = controls_event(&controls, &physical, packet, 64, &changed);
        if (!assembling && (physical.mask || physical.preset)) physical_at = now;
        if (result == E_BAD) { stop_at("physical-packet"); return; }
        if (result != E_CHANGED) continue;
        slot = controls.raw[0][4];
        for (unsigned m = 0; m < CONTROL_MODULES; m++) {
            if (changed & (1u << m)) {
                memcpy(actual.raw[m], controls.raw[m], 64);
                memcpy(actual.latent[m], controls.latent[m], 64);
                pending[m] = false;
            }
            actual.raw[m][4] = slot;
            actual.raw[m][7] = controls.raw[m][7];
        }
        physical_updates++;
        tone_notify(changed, previous_slot != slot);
    }
    if (physical.mask || physical.preset) {
        if (now - physical_at >= 2000) stop_at("physical-incomplete");
        return;
    }
    if (tuner_pending) {
        control_command = (control_plan_t){.module = CONTROL_MODULES + 1};
        control_command.expected[0] = tuner_enabled;
        tuner_pending = false;
        tuning = operation = true;
        operation_at = now;
        if (epoch == UINT32_MAX) { stop_at("epoch-overflow"); return; }
        atomic_store(&write_in_flight, true);
        atomic_store(&gain_requested, ++epoch);
        return;
    }
    if (load_slot && !selecting) {
        selection_state = actual;
        selection_event = (control_event_t){0};
        memset(selection_name, 0, sizeof(selection_name));
        control_command = (control_plan_t){.module = CONTROL_MODULES};
        control_command.expected[4] = load_slot - 1;
        selecting = operation = true;
        operation_at = now;
        if (epoch == UINT32_MAX) { stop_at("epoch-overflow"); return; }
        atomic_store(&write_in_flight, true);
        atomic_store(&gain_requested, ++epoch);
        return;
    }

    if (clearing) {
        for (unsigned m = 1; m < CONTROL_MODULES; m++) {
            const uint8_t *a = actual.raw[m], *w = controls.raw[m];
            if (!(a[16] || a[17]) || !memcmp(a + 16, w + 16, 3)) continue;
            control_request_t bypass = {.type = C_BOOL, .number = 1};
            snprintf(bypass.node, sizeof(bypass.node), "%s", controls_node(m));
            snprintf(bypass.parameter, sizeof(bypass.parameter), "bypass");
            static control_state_t clear_state;
            clear_state = actual;
            clear_state.gate = (control_gate_t){0};
            if (!controls_plan(&clear_state, &bypass, &edit_plan)) { stop_at("preset-clear"); return; }
            goto submit;
        }
        clearing = false;
    }

    for (unsigned i = 0; i < CONTROL_MODULES; i++) {
        unsigned m = (next_module + i) % CONTROL_MODULES;
        if (!pending[m]) continue;
        pending[m] = false;
        if (!controls_diff(&controls, &actual, m, &edit_plan)) {
            memcpy(actual.latent[m], controls.latent[m], 64);
            continue;
        }
        goto submit;
    }
    actual.gate = controls.gate;
    recovering = false;
    if (expr_pending || save_pending) {
        control_command = (control_plan_t){.module = CONTROL_MODULES + (expr_pending ? 3 : 2)};
        if (expr_pending) {
            control_command.expected[16] = wanted_expression;
            expr_pending = false; expr_active = true;
        } else {
            control_command.expected[4] = save_slot - 1;
            memcpy(control_command.expected + 16, save_name, sizeof(save_name));
            save_pending = false; save_active = true;
        }
        if (epoch == UINT32_MAX) { stop_at("epoch-overflow"); return; }
        operation = true; operation_at = now;
        atomic_store(&write_in_flight, true);
        atomic_store(&gain_requested, ++epoch);
    }
    return;

submit:
    if (epoch == UINT32_MAX) { stop_at("epoch-overflow"); return; }
    control_command = edit_plan;
    target = edit_plan.expected[33];
    next_module = (edit_plan.module + 1) % CONTROL_MODULES;
    operation = true;
    operation_at = now;
    atomic_store(&write_in_flight, true);
    atomic_store(&gain_requested, ++epoch);
}

#ifndef TORINO_CONTROLLER_TEST
void tud_event_hook_cb(uint8_t port, uint32_t event, bool in_isr) {
    (void)port; (void)in_isr;
    if (event == DCD_EVENT_BUS_RESET || event == DCD_EVENT_UNPLUGGED)
        atomic_store(&reset_needed, true);
}

uint16_t tud_hid_get_report_cb(uint8_t idx, uint8_t id, hid_report_type_t type,
                               uint8_t *buffer, uint16_t length) {
    (void)idx; (void)id; (void)type; (void)buffer; (void)length;
    return 0;
}

void tud_hid_set_report_cb(uint8_t idx, uint8_t id, hid_report_type_t type,
                          const uint8_t *buffer, uint16_t length) {
    if (!idx && !id && type == HID_REPORT_TYPE_OUTPUT) {
        trace_record(true, to_ms_since_boot(get_absolute_time()), buffer, length);
        if (!usb_paused && !atomic_load(&reset_needed)) tone_receive(buffer, length);
    }
}

void tud_hid_report_complete_cb(uint8_t idx, const uint8_t *report, uint16_t length) {
    if (idx || !submitted || atomic_load(&reset_needed)) return;
    submitted = false;
    if (length != 64 || memcmp(report, in_report, 64)) { stop_at("tone-send-completion"); return; }
    trace_record(false, to_ms_since_boot(get_absolute_time()), report, length);
    tone_sent();
}

void tud_hid_report_failed_cb(uint8_t idx, hid_report_type_t type,
                              const uint8_t *report, uint16_t length) {
    (void)idx; (void)type; (void)report; (void)length;
    if (!usb_paused) reset_tone();
}

static void native_tick(void) {
    uint8_t packet[64];
    if (!tud_cdc_connected()) {
        (void)native_pop(&native_reports, packet);
        return;
    }
    char line[144];
    if (tud_cdc_write_available() < sizeof(line) || !native_pop(&native_reports, packet)) return;
    static const char hex[] = "0123456789abcdef";
    memcpy(line, "NATIVE ", 7);
    for (unsigned i = 0; i < 64; i++) {
        line[7 + 2*i] = hex[packet[i] >> 4];
        line[8 + 2*i] = hex[packet[i] & 15];
    }
    line[135] = '\n';
    tud_cdc_write(line, 136);
    tud_cdc_write_flush();
}

static void trace_tick(void) {
    read_trace_t event;
    if (!tud_cdc_connected()) {
        if (native_pop(&read_traces, (uint8_t *)&event)) atomic_fetch_add(&trace_drops, 1);
        return;
    }
    char line[256];
    if (tud_cdc_write_available() < sizeof(line) || !native_pop(&read_traces, (uint8_t *)&event)) return;
    int n = snprintf(line, sizeof(line),
        "READ cycle=%lu report=%lu cmd_us=%lu sent_us=%lu rx_us=%lu rearm_us=%lu gap_us=%lu loop_us=%lu poll=%lu:%lu:%lu drops=%u\n",
        (unsigned long)event.cycle, (unsigned long)event.report,
        (unsigned long)event.command_us, (unsigned long)event.sent_us,
        (unsigned long)event.received_us, (unsigned long)event.rearm_us,
        (unsigned long)event.gap_us, (unsigned long)event.loop_us,
        (unsigned long)((event.poll >> 8) & 255), (unsigned long)(event.poll & 255),
        (unsigned long)(event.poll >> 16), atomic_load(&trace_drops));
    if (n > 0 && (size_t)n < sizeof(line)) {
        tud_cdc_write(line, n);
        tud_cdc_write_flush();
    }
}

static void usb_trace_tick(void) {
    char line[TRACE_LINE];
    if (!tud_cdc_connected() || tud_cdc_write_available() < sizeof(line)) return;
    size_t length = trace_line(line, sizeof(line));
    if (length) {
        tud_cdc_write(line, length);
        tud_cdc_write_flush();
    }
}

static void diagnostic(uint32_t now) {
    static uint32_t last;
    if (now - last < 1000 || !tud_cdc_connected()) return;
    last = now;
    char line[768], tone[128];
    tone_diag(tone, sizeof(tone));
    unsigned state = atomic_load(&link);
    const char *error = state == L_FAILED ? (probe.error ? probe.error : recovery_capture.error) : NULL;
    int n = snprintf(line, sizeof(line),
        "host=%u failed=%u adopted=%u confirming=%u operation=%u refresh=%u gain=%u epoch=%u version=%u error=%s fault=%s host_fault=%s target=%u port=%u retries=%u sessions=%u native_rx=%u native_head=%08x physical=%u reads=%u slot=%u model=%u control=%s:%s:%u:%.6g:%s:%s usb_trace_drops=%u %s\n",
        state, failed, adopted, confirming, operation, refresh, controls.raw[0][33], epoch, version,
        error ? error : "-", fault, host_error ? host_error : "-",
        target,
        atomic_load(&host_port), atomic_load(&discovery_retries), atomic_load(&host_sessions),
        atomic_load(&idle_count), atomic_load(&idle_header), physical_updates,
        atomic_load(&settings_reads), slot + 1u,
        (unsigned)controls.raw[0][16] | (unsigned)controls.raw[0][17] << 8,
        last_control.node, last_control.replace ? "model" : last_control.parameter,
        last_control.type, (double)last_control.number, last_control.text, control_result, trace_lost(), tone);
    if (n > 0 && (size_t)n < sizeof(line) && tud_cdc_write_available() >= (unsigned)n) {
        tud_cdc_write(line, n);
        tud_cdc_write_flush();
    }
}

int main(void) {
    set_sys_clock_khz(120000, true);
    watchdog_enable(5000, true);
    multicore_launch_core1(host_main);
    const tusb_rhport_init_t init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    if (!tud_rhport_init(0, &init)) for (;;) tight_loop_contents();
    for (;;) {
        trace_enable(tud_cdc_connected());
        tud_task_ext(0, false);
        feed_watchdog();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (atomic_exchange(&reset_needed, false)) reset_tone();
        host_tick(now);
        store_tick();
        tone_tick();
        usb_trace_tick();
        trace_tick();
        native_tick();
        diagnostic(now);
        if (usb_paused && amp_ready() && now - usb_at >= 250) {
            tud_connect();
            usb_paused = false;
        }
        if (submitted && now - sent_at >= 1000) reset_tone();
        if (!usb_paused && !submitted && tud_hid_ready() && tone_next(in_report)) {
            submitted = true; sent_at = now;
            if (!tud_hid_report(0, in_report, 64)) reset_tone();
        }
        tight_loop_contents();
    }
}
#endif
