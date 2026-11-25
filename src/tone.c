#include "tone.h"
#include "preset.h"
#include "store.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint8_t assembly[16384], tx[160][64];
static size_t assembled, tx_head, tx_count;
static bool fragmented, syncing, notify_preset;
static unsigned notify_modules;
static control_state_t shown, before_preview;
static bool audition, loading, tuning;
static unsigned tuner_context;
static char preview_name[33];
typedef struct { unsigned field, value, second, flag; char name[20]; control_request_t control; char json[6144]; } request_t;
static bool storing, store_acked;
static unsigned store_notice;
static request_t requests[16], active;
static unsigned head, count, received, rejected, last_field;

typedef struct { const uint8_t *p; size_t n; } span_t;
typedef struct { unsigned number, wire; uint64_t value; span_t bytes; } field_t;

static bool varint(span_t *s, uint64_t *value) {
    *value = 0;
    for (unsigned shift = 0; shift < 70; shift += 7) {
        if (!s->n) return false;
        uint8_t b = *s->p++; s->n--;
        if (shift == 63 && b > 1) return false;
        *value |= (uint64_t)(b & 127) << shift;
        if (!(b & 128)) return true;
    }
    return false;
}

static bool field(span_t *s, field_t *f) {
    uint64_t tag, n;
    *f = (field_t){0};
    if (!varint(s, &tag) || !(tag >> 3) || (tag >> 3) > 0x1fffffff) return false;
    f->number = tag >> 3; f->wire = tag & 7;
    if (!f->wire) return varint(s, &f->value);
    if (f->wire == 2) { if (!varint(s, &n)) return false; }
    else if (f->wire == 5 || f->wire == 1) n = f->wire == 5 ? 4 : 8;
    else return false;
    if (n > s->n) return false;
    f->bytes = (span_t){s->p, (size_t)n};
    s->p += n; s->n -= n;
    return true;
}

static bool scalar(span_t s, unsigned wanted, unsigned *value) {
    bool found = false;
    while (s.n) {
        field_t f;
        if (!field(&s, &f)) return false;
        if (f.number != wanted) continue;
        if (found || f.wire || f.value > UINT32_MAX) return false;
        found = true; *value = f.value;
    }
    return found;
}

static bool copy_text(span_t s, char *text, size_t size) {
    if (!s.n || s.n >= size) return false;
    for (size_t i = 0; i < s.n; i++) if (s.p[i] < 32 || s.p[i] > 126) return false;
    memcpy(text, s.p, s.n); text[s.n] = 0;
    return true;
}

static bool parse(span_t s, request_t *r) {
    bool ack_seen = false, payload_seen = false;
    uint64_t ack = 0;
    span_t payload = {0};
    *r = (request_t){0};
    while (s.n) {
        field_t f;
        if (!field(&s, &f)) return false;
        if (f.number == 1 && !f.wire && !ack_seen) { ack = f.value; ack_seen = true; }
        else if (f.wire == 2 && !payload_seen) {
            payload = f.bytes; r->field = f.number; payload_seen = true;
        } else return false;
    }
    if (!ack_seen || !payload_seen || (ack && !(ack == 2 && r->field == 200))) return false;
    if (r->field == 34 || r->field == 39) {
        bool node = false, param = false, value = false;
        control_request_t *c = &r->control;
        c->replace = r->field == 39;
        while (payload.n) {
            field_t f;
            if (!field(&payload, &f)) return false;
            if (f.number == 1) {
                if (node || f.wire != 2 || !copy_text(f.bytes, c->node, sizeof(c->node))) return false;
                node = true;
            } else if (f.number == 2) {
                char *out = c->replace ? c->text : c->parameter;
                size_t size = c->replace ? sizeof(c->text) : sizeof(c->parameter);
                if (param || f.wire != 2 || !copy_text(f.bytes, out, size)) return false;
                param = true;
            } else if (!c->replace && f.number >= 3 && f.number <= 6) {
                if (value) return false;
                value = true; c->type = (enum control_type)f.number;
                if (f.number == 3 && f.wire == 5) {
                    uint32_t bits = (uint32_t)f.bytes.p[0] | (uint32_t)f.bytes.p[1] << 8 |
                        (uint32_t)f.bytes.p[2] << 16 | (uint32_t)f.bytes.p[3] << 24;
                    memcpy(&c->number, &bits, 4);
                    if (!isfinite(c->number)) return false;
                } else if (f.number == 4 && f.wire == 2) {
                    if (!copy_text(f.bytes, c->text, sizeof(c->text))) return false;
                } else if (f.number == 5 && !f.wire && f.value <= UINT32_MAX) {
                    c->number = (float)((int32_t)(f.value >> 1) ^ -(int32_t)(f.value & 1));
                } else if (f.number == 6 && !f.wire && f.value <= 1) c->number = f.value;
                else return false;
            } else return false;
        }
        return node && param && (value || c->replace);
    }
    switch (r->field) {
    case 41: case 43:
        return scalar(payload, 1, &r->value) && r->value >= 1 && r->value <= 24 &&
            scalar(payload, 2, &r->second) && r->second >= 1 && r->second <= 24;
    case 53:
        return scalar(payload, 1, &r->value) && r->value == 1;
    case 54: case 55: case 57: {
        bool text_seen = false;
        span_t body = payload;
        while (body.n) {
            field_t f;
            if (!field(&body, &f)) return false;
            if (f.number != 1) continue;
            if (text_seen || f.wire != 2) return false;
            text_seen = true;
            if (r->field == 55) {
                if (!f.bytes.n || f.bytes.n >= sizeof(r->json) || memchr(f.bytes.p, 0, f.bytes.n)) return false;
                memcpy(r->json, f.bytes.p, f.bytes.n);
            } else if (!copy_text(f.bytes, r->control.text, sizeof(r->control.text))) return false;
        }
        if (!text_seen || !scalar(payload, r->field == 55 ? 3 : 2, &r->value) ||
            r->value < 1 || r->value > 24) return false;
        if (r->field == 55) return scalar(payload, 2, &r->flag) && r->flag <= 1;
        snprintf(r->name, sizeof(r->name), "%.19s", r->control.text);
        size_t n = strlen(r->name);
        while (n && r->name[n - 1] == ' ') r->name[--n] = 0;
        return true;
    }
    case 58: {
        field_t f;
        if (!field(&payload, &f) || payload.n || f.number != 1 || f.wire != 2 ||
            !f.bytes.n || f.bytes.n >= sizeof(r->json) || memchr(f.bytes.p, 0, f.bytes.n)) return false;
        memcpy(r->json, f.bytes.p, f.bytes.n);
        return true;
    }
    case 33:
        return scalar(payload, 1, &r->value) && r->value >= 1 && r->value <= 24;
    case 113: {
        unsigned mode;
        return scalar(payload, 1, &r->value) &&
            scalar(payload, 2, &mode) && mode == 0 && (r->value <= 1 || r->value == 6 || r->value == 7 ||
                          r->value == 10 || r->value == 11);
    }
    case 200: case 201:
        return scalar(payload, 1, &r->value) && r->value <= (r->field == 200 ? 5u : 1u);
    case 105:
        return scalar(payload, 1, &r->value) && r->value >= 1 && r->value <= 60;
    case 60: case 62: case 102: case 104: case 106: case 108: case 110: case 202:
        return scalar(payload, 1, &r->value) && r->value == 1;
    default: r->field = 0; return true;
    }
}

void tone_receive(const uint8_t *data, size_t length) {
    received++;
    if (length < 3 || length > 64 || !data[1] || data[1] > 61 || data[1] + 2u > length) goto bad;
    if (data[0] == 0x33) { assembled = 0; fragmented = true; }
    else if (data[0] == 0x34 && fragmented) {}
    else if (data[0] == 0x35) { if (!fragmented) assembled = 0; fragmented = false; }
    else goto bad;
    if (assembled + data[1] > sizeof(assembly)) goto bad;
    memcpy(assembly + assembled, data + 2, data[1]); assembled += data[1];
    if (fragmented) return;
    if (count == 16) goto bad;
    static request_t r;
    if (!parse((span_t){assembly, assembled}, &r)) {
        rejected++;
        r = (request_t){.field = UINT32_MAX};
    }
    last_field = r.field;
    requests[(head + count++) % 16] = r;
    assembled = 0;
    return;
bad:
    rejected++;
    assembled = 0; fragmented = false;
    if (count < 16) requests[(head + count++) % 16] = (request_t){.field = UINT32_MAX};
    else { amp_stop(); tone_reset(); }
}

static size_t put(uint8_t *p, uint32_t value) {
    size_t n = 0;
    do { p[n++] = (value & 127) | (value >= 128 ? 128 : 0); value >>= 7; } while (value);
    return n;
}

static void reply(unsigned field_no, const uint8_t *payload, size_t length, uint8_t ack) {
    static uint8_t message[8192];
    message[0] = 8; message[1] = ack;
    size_t n = 2;
    if (length + 12 > sizeof(message)) return;
    n += put(message + n, (field_no << 3) | 2);
    n += put(message + n, length);
    memcpy(message + n, payload, length); n += length;
    size_t reports = (n + 60) / 61;
    if (tx_count + reports > 160) return;
    for (size_t offset = 0; offset < n; offset += 61) {
        uint8_t *p = tx[(tx_head + tx_count++) % 160];
        size_t chunk = n - offset < 61 ? n - offset : 61;
        memset(p, 0, 64);
        p[1] = offset + chunk == n ? 0x35 : offset == 0 ? 0x33 : 0x34;
        p[2] = chunk;
        memcpy(p + 3, message + offset, chunk);
    }
}

static void failure(uint8_t code) {
    uint8_t payload[] = {8, code};
    reply(200, payload, sizeof(payload), 2);
}

static void preset_name(unsigned slot, bool current, const char *stored, char text[193]) {
    const char *name = stored ? stored : current && audition ? preview_name : amp_name(slot);
    if (!name) { snprintf(text, 193, "TORINO %02u", slot); return; }

    size_t n = 0;
    for (unsigned i = 0; i < 32 && name[i]; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c == '"' || c == '\\') { text[n++] = '\\'; text[n++] = c; }
        else if (c < 32 || c > 126) n += (size_t)snprintf(text + n, 193 - n, "\\u%04x", c);
        else text[n++] = c;
    }
    text[n] = 0;
}

static void preset(unsigned slot, bool current, bool notification, unsigned response) {
    static char json[6144], edges[1536], nodes[4096];
    static control_state_t stored;
    char stored_name[33];
    bool cached = !current && store_snapshot(slot, &stored, stored_name);
    if (!(cached ? controls_json(&stored, nodes, sizeof(nodes)) : amp_nodes(nodes, sizeof(nodes)))) { failure(1); return; }
    const char *path[7];
    if (!(cached ? controls_path(&stored, path) : amp_path(path))) { failure(1); return; }
    size_t used = 0;
    for (unsigned i = 0; i < 6; i++) for (unsigned ch = 0; ch < 2; ch++) {
        int n = snprintf(edges + used, sizeof(edges) - used,
            "%s{\"input\":{\"index\":%u,\"nodeId\":\"%s\"},\"output\":{\"index\":%u,\"nodeId\":\"%s\"}}",
            used ? "," : "", ch, path[i], ch, path[i + 1]);
        if (n < 0 || (size_t)n >= sizeof(edges) - used) { failure(1); return; }
        used += n;
    }
    char name[193];
    preset_name(slot, current, cached ? stored_name : NULL, name);
    int n = snprintf(json, sizeof(json),
        "{\"nodeId\":\"preset\",\"nodeType\":\"preset\",\"version\":\"1.1\",\"numInputs\":2,\"numOutputs\":2,"
        "\"info\":{\"displayName\":\"%s\",\"product_id\":\"mustang-lt\",\"author\":\"Torino\","
        "\"timestamp\":0,\"created_at\":0,\"bpm\":120,\"preset_id\":\"\",\"source_id\":\"\",\"is_factory_default\":false},"
        "\"audioGraph\":{\"connections\":[%s],\"nodes\":%s}}",
        name, edges, nodes);
    if (n < 0 || (size_t)n >= sizeof(json)) { failure(1); return; }
    static uint8_t payload[6200];
    size_t len = 0;
    payload[len++] = 10; len += put(payload + len, n);
    memcpy(payload + len, json, n); len += n;
    if (response != 59) {
        payload[len++] = 16; payload[len++] = slot;
        if (current && response != 31) { payload[len++] = 24; payload[len++] = amp_dirty(); }
    }
    reply(response ? response : current ? 32 : 31, payload, len, notification ? 0 : 2);
}

static void saved_reply(unsigned slot) {
    const char *name = amp_name(slot);
    size_t length = name ? strlen(name) : 0;
    if (length > 32) length = 32;
    uint8_t payload[40] = {10, (uint8_t)length};
    if (length) memcpy(payload + 2, name, length);
    payload[length + 2] = 16; payload[length + 3] = slot;
    reply(50, payload, length + 4, 2);
}

static void control_reply(const control_request_t *c, bool notification) {
    uint8_t payload[160];
    size_t n = 0, size = strlen(c->node);
    payload[n++] = 10; n += put(payload + n, size);
    memcpy(payload + n, c->node, size); n += size;
    const char *second = c->replace ? c->text : c->parameter;
    size = strlen(second);
    payload[n++] = 18; n += put(payload + n, size);
    memcpy(payload + n, second, size); n += size;
    if (!c->replace) {
        if (c->type == C_FLOAT) {
            payload[n++] = 29;
            uint32_t bits; memcpy(&bits, &c->number, 4);
            for (unsigned i = 0; i < 4; i++) payload[n++] = bits >> (8 * i);
        } else if (c->type == C_STRING) {
            size = strlen(c->text); payload[n++] = 34; n += put(payload + n, size);
            memcpy(payload + n, c->text, size); n += size;
        } else if (c->type == C_BOOL) { payload[n++] = 48; payload[n++] = c->number != 0; }
        else { failure(0); return; }
    }
    reply(c->replace ? 40 : notification ? 36 : 35, payload, n, notification ? 0 : 2);
}

void tone_notify(unsigned modules, bool preset_changed) {
    notify_modules |= modules;
    notify_preset |= preset_changed;
}

static void remember(unsigned module, const control_state_t *state) {
    memcpy(shown.raw[module], state->raw[module], 64);
    memcpy(shown.latent[module], state->latent[module], 64);
    if (module == 1) shown.gate = state->gate;
    shown.valid = true;
}

static void changes(void) {
    static control_state_t current;
    static control_request_t old[CONTROL_VALUES], values[CONTROL_VALUES];
    amp_snapshot(&current);
    unsigned modules = notify_modules;
    bool selected = notify_preset;
    notify_modules = 0; notify_preset = false;
    bool refresh = selected || !shown.valid;
    for (unsigned m = 1; !refresh && m < CONTROL_MODULES; m++) {
        if (!(modules & (1u << m))) continue;
        bool bypass = m == 1 && current.gate.selected ? current.gate.bypassed :
            !(current.raw[m][16] || current.raw[m][17]);
        bool was_bypass = m == 1 && shown.gate.selected ? shown.gate.bypassed :
            !(shown.raw[m][16] || shown.raw[m][17]);
        const uint8_t *raw = bypass ? current.latent[m] : current.raw[m];
        const uint8_t *before = was_bypass ? shown.latent[m] : shown.raw[m];
        if (strcmp(controls_model(&current, m), controls_model(&shown, m)) ||
            bypass != was_bypass || raw[18] != before[18]) refresh = true;
    }
    if (selected) {
        audition = false;
        preset(amp_slot(), true, true, 31);
        uint8_t payload[] = {8, amp_slot()};
        reply(37, payload, sizeof(payload), 0);
    }
    if (refresh) {
        preset(amp_slot(), true, true, 0);
        shown = current;
        modules = 0;
    }
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        if (!(modules & (1u << m))) continue;
        const char *name = controls_model(&current, m);
        const char *previous = controls_model(&shown, m);
        bool replaced = !previous || strcmp(previous, name);
        if (replaced) {
            control_request_t model = {.replace = true};
            snprintf(model.node, sizeof(model.node), "%s", controls_node(m));
            snprintf(model.text, sizeof(model.text), "%s", name);
            control_reply(&model, true);
        }
        size_t n = controls_values(&current, controls_node(m), values);
        size_t before = controls_values(&shown, controls_node(m), old);
        for (size_t i = 0; i < n; i++) {
            bool same = false;
            if (!replaced) for (size_t j = 0; j < before; j++)
                if (!strcmp(values[i].parameter, old[j].parameter) && values[i].type == old[j].type &&
                    values[i].number == old[j].number && !strcmp(values[i].text, old[j].text)) same = true;
            if (!same) control_reply(&values[i], true);
        }
        remember(m, &current);
    }
    uint8_t dirty[] = {8, amp_dirty()};
    reply(38, dirty, sizeof(dirty), 0);
}

void tone_tick(void) {
    if (tx_count) return;
    if (!shown.valid && amp_connected() && !notify_modules) amp_snapshot(&shown);
    if (storing) {
        if (store_busy()) return;
        if (!store_ok()) { storing = false; failure(1); return; }
        bool reorder = active.field == 41 || active.field == 43;
        if (reorder && !store_acked) {
            uint8_t status[] = {8, active.value, 16, active.second};
            reply(active.field + 1, status, sizeof(status), 2);
            store_acked = true;
            return;
        }
        while (store_notice <= 24) {
            unsigned slot = store_notice++;
            if (store_changed(slot)) { preset(slot, false, true, 31); return; }
        }
        storing = false;
        if (!reorder) {
            if (active.field != 57) audition = false;
            if (active.field == 55) preset(active.value, false, false, 56);
            else saved_reply(active.value);
        }
        preset(amp_slot(), true, true, 0);
        amp_snapshot(&shown);
        return;
    }
    if (tuning) {
        if (!amp_connected()) { tuning = false; failure(1); return; }
        if (amp_tuning()) return;
        tuning = false;
        uint8_t status[] = {8, tuner_context, 16, 0};
        reply(113, status, sizeof(status), 2);
        return;
    }
    if (loading) {
        if (!amp_connected()) { loading = false; failure(1); return; }
        if (amp_loading()) return;
        loading = audition = false;
        preset(amp_slot(), true, true, 31);
        uint8_t selected[] = {8, amp_slot()};
        reply(37, selected, sizeof(selected), 2);
        preset(amp_slot(), true, true, 0);
        amp_snapshot(&shown);
        return;
    }
    if (!count) {
        if (notify_modules || notify_preset) changes();
        return;
    }
    active = requests[head]; head = (head + 1) % 16; count--;
    uint8_t payload[8] = {8, 1};
    switch (active.field) {
    case 200: case 201: return;
    case 113:
        if (active.value == 10 || active.value == 11) {
            payload[1] = active.value; payload[2] = 16; payload[3] = 0;
            reply(113, payload, 4, 2); return;
        }
        if (active.value == 6 || active.value == 7) {
            if (!amp_tuner(active.value == 6)) { failure(1); return; }
            tuning = true; tuner_context = active.value;
            return;
        }
        if (active.value && !syncing) { failure(2); return; }
        syncing = !active.value;
        payload[1] = active.value; payload[2] = 16; payload[3] = 0;
        reply(113, payload, 4, 2); return;
    case 102: reply(103, (const uint8_t *)"\12\5" "2.1.4", 7, 2); return;
    case 106: case 110:
        memset(payload, 0, 5); payload[0] = 13;
        reply(active.field + 1, payload, 5, 2); return;
    case 108: reply(109, (const uint8_t *)"\10\1\10\2\10\3\10\4", 8, 2); return;
    case 62: payload[1] = audition; reply(63, payload, 2, 2); return;
    case 41: case 43: case 53: case 54: case 55: case 57: {
        static store_request_t request;
        request = (store_request_t){.from = active.value, .to = active.value, .load = true};
        if (active.field == 41 || active.field == 43) {
            request.kind = active.field == 41 ? S_MOVE : S_SWAP;
            request.to = active.second;
        } else if (active.field == 57) {
            request.kind = S_RENAME;
            memcpy(request.name, active.name, sizeof(request.name));
        } else {
            request.kind = S_SAVE;
            if (active.field == 53) {
                active.value = request.from = request.to = amp_slot();
                const char *name = audition ? preview_name : amp_name(amp_slot());
                snprintf(request.name, sizeof(request.name), "%.19s", name ? name : "TORINO");
            } else if (active.field == 55) {
                static control_state_t base;
                char name[33]; amp_snapshot(&base);
                if (!preset_decode(active.json, strlen(active.json), &base, &request.state, name)) { failure(1); return; }
                snprintf(request.name, sizeof(request.name), "%.19s", name);
                request.draft = true; request.load = active.flag;
            } else memcpy(request.name, active.name, sizeof(request.name));
        }
        if (!store_start(&request)) { failure(1); return; }
        storing = true; store_acked = false; store_notice = 1;
        return;
    }
    case 33:
        if (!amp_select(active.value)) { failure(1); return; }
        loading = true;
        return;
    case 58: {
        static control_state_t base, draft;
        char name[33] = {0};
        amp_snapshot(&base);
        if (!preset_decode(active.json, strlen(active.json), &base, &draft, name) || !amp_apply(&draft)) {
            failure(1); return;
        }
        if (!audition) before_preview = base;
        audition = true;
        memcpy(preview_name, name, sizeof(preview_name));
        preset(amp_slot(), true, false, 59);
        shown = draft;
        return;
    }
    case 60:
        if (audition && !amp_apply(&before_preview)) { failure(1); return; }
        audition = false;
        reply(61, payload, 2, 2);
        preset(amp_slot(), true, true, 0);
        amp_snapshot(&shown);
        return;
    case 202: payload[1] = amp_connected(); reply(203, payload, 2, 2); return;
    case 105:
        if (!amp_connected()) { failure(1); return; }
        preset(active.value, false, false, 0); return;
    case 104:
        if (!amp_connected()) { failure(1); return; }
        preset(amp_slot(), true, false, 0); return;
    case 34: case 39:
        if (!amp_control(&active.control)) { failure(1); return; }
        static control_state_t accepted;
        amp_snapshot(&accepted);
        if (accepted.gate.selected && !strcmp(active.control.node, "stomp") &&
            !strcmp(active.control.parameter, "threshold") && !active.control.replace)
            active.control.number = -96 + 6 * accepted.gate.threshold;
        control_reply(&active.control, false);
        for (unsigned m = 0; m < CONTROL_MODULES; m++)
            if (!strcmp(active.control.node, controls_node(m))) remember(m, &accepted);
        if (active.control.replace) {
            static control_request_t values[CONTROL_VALUES];
            size_t n = amp_values(active.control.node, values);
            for (size_t i = 0; i < n; i++) control_reply(&values[i], true);
        }
        payload[1] = amp_dirty();
        reply(38, payload, 2, 0);
        return;
    default: failure(active.field == UINT32_MAX ? 2 : 0); return;
    }
}

bool tone_next(uint8_t report[64]) {
    if (!tx_count) return false;
    memcpy(report, tx[tx_head], 64);
    return true;
}

void tone_sent(void) {
    if (tx_count) { tx_head = (tx_head + 1) % 160; tx_count--; }
}

size_t tone_diag(char *text, size_t size) {
    int n = snprintf(text, size, "rx=%u rejected=%u field=%u queue=%u tx=%u",
        received, rejected, last_field, count, (unsigned)tx_count);
    return n > 0 && (size_t)n < size ? (size_t)n : 0;
}

void tone_reset(void) {
    assembled = tx_head = tx_count = head = count = 0;
    fragmented = syncing = notify_preset = loading = audition = tuning = storing = false;
    notify_modules = 0;
    shown.valid = false;
}
