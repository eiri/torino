#include "tone.h"
#include "store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static store_request_t storage;
static bool storage_busy, storage_success;
static unsigned storage_changes;
bool store_start(const store_request_t *request) { storage = *request; storage_busy = true; return true; }
bool store_busy(void) { return storage_busy; }
bool store_ok(void) { return storage_success; }
bool store_snapshot(unsigned slot, control_state_t *state, char name[33]) {
    (void)slot; (void)state; (void)name; return false;
}
bool store_changed(unsigned slot) { return slot >= 1 && slot <= 24 && (storage_changes & (1u << (slot - 1))); }

static unsigned sets, stops, applies;
static unsigned selected_slot = 14, pending_slot;
static bool slot_loading;
static bool valid = true, connected = true;
static uint8_t cached = 51, physical_model = 117;
static uint8_t message[8192];
static unsigned fields[64], acks[64], replies;
static bool snapshot_override;
static control_state_t physical_snapshot;

static bool tuner_busy, tuner_enabled;
bool amp_tuning(void) { return tuner_busy; }
bool amp_tuner(bool enabled) {
    if (!connected || tuner_busy) return false;
    tuner_busy = true; tuner_enabled = enabled;
    return true;
}
bool amp_connected(void) { return connected; }
void amp_stop(void) { stops++; }
static control_request_t last;
bool amp_control(const control_request_t *r) {
    if (!connected || !valid) return false;
    last = *r; sets++; return true;
}
size_t amp_values(const char *node, control_request_t values[CONTROL_VALUES]) {
    (void)node;
    if (!last.replace && (!strcmp(last.parameter, "bypass") || !strcmp(last.parameter, "sag"))) return 0;
    values[0] = last;
    if (last.replace) {
        values[0] = (control_request_t){.node = "amp", .parameter = "gain", .type = C_FLOAT, .number = cached / 255.0f};
    }
    return 1;
}
bool amp_dirty(void) { return cached != 51; }
unsigned amp_slot(void) { return selected_slot; }
bool amp_loading(void) { return slot_loading; }
bool amp_select(unsigned slot) {
    if (!connected || slot_loading || slot < 1 || slot > 24) return false;
    pending_slot = slot; slot_loading = true;
    return true;
}
bool amp_apply(const control_state_t *state) {
    if (!connected || slot_loading) return false;
    physical_snapshot = *state; snapshot_override = true; applies++;
    return true;
}
static const char *stored_name = "Beauty Clean";
const char *amp_name(unsigned slot) {
    if (slot == 1) return "Brutal Metal II";
    return slot == 14 ? stored_name : NULL;
}
bool amp_path(const char *path[7]) {
    const char *fixed[] = {"preset", "stomp", "mod", "amp", "delay", "reverb", "preset"};
    memcpy(path, fixed, sizeof(fixed));
    return true;
}
void amp_snapshot(control_state_t *state) {
    if (snapshot_override) { *state = physical_snapshot; return; }
    *state = (control_state_t){.valid = true};
    state->raw[0][16] = physical_model; state->raw[0][33] = cached;
    memcpy(state->latent, state->raw, sizeof(state->raw));
}
size_t amp_nodes(char *json, size_t size) {
    control_state_t state;
    amp_snapshot(&state);
    return controls_json(&state, json, size);
}

static void request(const uint8_t *bytes, size_t size) {
    uint8_t report[64] = {0x35};
    assert(size <= 61);
    report[1] = (uint8_t)size;
    memcpy(report + 2, bytes, size);
    tone_receive(report, sizeof(report));
    tone_tick();
}

static void knob(const char *node, const char *parameter, float value) {
    uint8_t bytes[61] = {8, 0, 0x92, 2};
    size_t n = 5, size = strlen(node);
    bytes[n++] = 10; bytes[n++] = (uint8_t)size;
    memcpy(bytes + n, node, size); n += size;
    size = strlen(parameter);
    bytes[n++] = 18; bytes[n++] = (uint8_t)size;
    memcpy(bytes + n, parameter, size); n += size;
    bytes[n++] = 29;
    uint32_t bits; memcpy(&bits, &value, 4);
    for (unsigned i = 0; i < 4; i++) bytes[n++] = bits >> (8 * i);
    bytes[4] = (uint8_t)(n - 5);
    request(bytes, n);
}

static size_t response(void) {
    uint8_t report[64];
    size_t size = 0;
    bool first = true;
    replies = 0;
    while (tone_next(report)) {
        if (first) {
            unsigned tag = report[5] & 127;
            if (report[5] & 128) tag |= (unsigned)report[6] << 7;
            assert(replies < sizeof(fields) / sizeof(fields[0]));
            acks[replies] = report[4];
            fields[replies++] = tag >> 3;
        }
        first = report[1] == 0x35;
        assert(size + report[2] <= sizeof(message));
        memcpy(message + size, report + 3, report[2]);
        size += report[2];
        tone_sent();
    }
    return size;
}

static bool contains(size_t size, const char *text) {
    size_t n = strlen(text);
    for (size_t i = 0; i + n <= size; i++)
        if (!memcmp(message + i, text, n)) return true;
    return false;
}

static size_t put_varint(uint8_t *out, size_t value) {
    size_t n = 0;
    do { out[n++] = (value & 127) | (value >= 128 ? 128 : 0); value >>= 7; } while (value);
    return n;
}

static void factory_request(const char *json) {
    static uint8_t bytes[6200];
    size_t size = strlen(json), payload = 1 + put_varint(bytes, size) + size;
    size_t n = 0;
    bytes[n++] = 8; bytes[n++] = 0; bytes[n++] = 0xd2; bytes[n++] = 3;
    n += put_varint(bytes + n, payload);
    bytes[n++] = 10; n += put_varint(bytes + n, size);
    memcpy(bytes + n, json, size); n += size;
    for (size_t i = 0; i < n; i += 61) {
        size_t chunk = n - i < 61 ? n - i : 61;
        uint8_t report[64] = {i + chunk == n ? 0x35 : i ? 0x34 : 0x33, (uint8_t)chunk};
        memcpy(report + 2, bytes + i, chunk);
        tone_receive(report, 64);
    }
    tone_tick();
}

static void preset_loads(void) {
    tone_reset();
    slot_loading = false;
    const uint8_t load[] = {8, 0, 0x8a, 2, 2, 8, 1};
    request(load, sizeof(load));
    assert(slot_loading && pending_slot == 1 && !response());
    tone_tick();
    assert(!response());
    selected_slot = pending_slot; slot_loading = false;
    tone_tick();
    size_t size = response();
    assert(size && replies == 3 && fields[0] == 31 && acks[0] == 0);
    assert(fields[1] == 37 && acks[1] == 2 && fields[2] == 32 && acks[2] == 0);
    assert(contains(size, "Brutal Metal II"));

    // A second switch must not publish an index or stale preset while waiting.
    uint8_t next[] = {8, 0, 0x8a, 2, 2, 8, 24};
    request(next, sizeof(next));
    assert(slot_loading && pending_slot == 24 && !response());
    tone_tick(); assert(!response());
    selected_slot = pending_slot; slot_loading = false;
    tone_tick(); size = response();
    assert(size && replies == 3 && fields[0] == 31 && fields[1] == 37 && fields[2] == 32);
    assert(acks[0] == 0 && acks[1] == 2 && acks[2] == 0 && contains(size, "TORINO 24"));
    assert(!contains(size, "Brutal Metal II") && !contains(size, "Beauty Clean"));
    selected_slot = 14;

    // Factory JSON arrives fragmented and changes only the accepted temporary draft.
    physical_snapshot = (control_state_t){.valid = true};
    physical_snapshot.raw[0][16] = 117; physical_snapshot.raw[0][33] = 51;
    physical_snapshot.available_count[0] = 2;
    physical_snapshot.available[0][0] = 117; physical_snapshot.available[0][1] = 241;
    memcpy(physical_snapshot.latent, physical_snapshot.raw, sizeof(physical_snapshot.raw));
    snapshot_override = true;
    char nodes[4096], json[6144];
    assert(amp_nodes(nodes, sizeof(nodes)));
    int n = snprintf(json, sizeof(json),
        "{\"nodeType\":\"preset\",\"info\":{\"product_id\":\"mustang-lt\",\"displayName\":\"Factory test\"},"
        "\"audioGraph\":{\"nodes\":%s,\"connections\":[", nodes);
    assert(n > 0);
    size_t used = n;
    const char *path[7]; assert(amp_path(path));
    for (unsigned i = 0; i < 6; i++) for (unsigned ch = 0; ch < 2; ch++) {
        n = snprintf(json + used, sizeof(json) - used,
            "%s{\"input\":{\"index\":%u,\"nodeId\":\"%s\"},\"output\":{\"index\":%u,\"nodeId\":\"%s\"}}",
            i || ch ? "," : "", ch, path[i], ch, path[i + 1]);
        assert(n > 0 && (size_t)n < sizeof(json) - used); used += n;
    }
    strcpy(json + used, "]}}");
    factory_request(json);
    size = response();
    assert(size && fields[0] == 59 && acks[0] == 2 && applies == 1);
    assert(contains(size, "\"displayName\":\"Factory test\""));
    const uint8_t status[] = {8, 0, 0xc2, 6, 2, 8, 1};
    request(status, sizeof(status));
    assert(contains(response(), "Factory test"));
    const uint8_t state[] = {8, 0, 0xf2, 3, 2, 8, 1};
    request(state, sizeof(state));
    assert(response() && fields[0] == 63 && message[6] == 1);

    // Invalid imports do not enter the controller. Exit restores pre-preview edits.
    factory_request("{}");
    assert(response() && fields[0] == 200 && applies == 1);
    const uint8_t exit[] = {8, 0, 0xe2, 3, 2, 8, 1};
    request(exit, sizeof(exit));
    assert(response() && fields[0] == 61 && fields[1] == 32 && applies == 2);
    assert(physical_snapshot.raw[0][33] == 51);

    // Tone receives the adapted preview, not the unsupported source model.
    char *model = strstr(json, "DUBS_Twin65"); assert(model);
    memcpy(model, "DUBS_AlienX", strlen("DUBS_Twin65"));
    factory_request(json);
    size = response();
    assert(size && fields[0] == 59 && acks[0] == 2 && applies == 3);
    assert(contains(size, "DUBS_LinearGain") && !contains(size, "DUBS_AlienX"));
    assert(physical_snapshot.raw[0][16] == 241);
    request(exit, sizeof(exit));
    assert(response() && applies == 4 && physical_snapshot.raw[0][16] == 117);
}

static size_t effect_update(unsigned module) {
    tone_notify(1u << module, false); tone_tick();
    size_t size = response();
    assert(size && replies == 2 && fields[0] == 32 && fields[1] == 38);
    assert(acks[0] == 0 && acks[1] == 0);
    return size;
}

static void storage_commands(void) {
    tone_reset();
    uint8_t busy[] = {8, 0, 0x8a, 7, 4, 8, 10, 16, 0};
    request(busy, sizeof(busy));
    assert(response() && fields[0] == 113 && acks[0] == 2 && message[6] == 10);
    busy[6] = 11;
    request(busy, sizeof(busy));
    assert(response() && fields[0] == 113 && acks[0] == 2 && message[6] == 11);
    uint8_t move[] = {8, 0, 0xca, 2, 4, 8, 1, 16, 2};
    request(move, sizeof(move));
    assert(storage_busy && storage.kind == S_MOVE && storage.from == 1 && storage.to == 2);
    assert(!response()); tone_tick(); assert(!response());
    storage_busy = false; storage_success = true; storage_changes = 3; tone_tick();
    assert(response() && replies == 1 && fields[0] == 42 && acks[0] == 2);
    tone_tick(); assert(response() && replies == 1 && fields[0] == 31);
    tone_tick(); assert(response() && replies == 1 && fields[0] == 31);
    tone_tick(); assert(response() && replies == 1 && fields[0] == 32);
    storage_changes = 0;
    move[2] = 0xda;
    request(move, sizeof(move));
    assert(storage.kind == S_SWAP && !response());
    storage_busy = false; tone_tick();
    assert(response() && fields[0] == 44 && acks[0] == 2);
    tone_tick(); assert(response() && fields[0] == 32);
    const uint8_t save[] = {8, 0, 0xaa, 3, 2, 8, 1};
    request(save, sizeof(save));
    assert(storage.kind == S_SAVE && storage.to == amp_slot() && storage.load && !response());
    storage_busy = false; tone_tick();
    assert(response() && fields[0] == 50 && acks[0] == 2);
    assert(message[5] == 10 && message[6] == strlen(amp_name(amp_slot())));
    assert(!memcmp(message + 7, amp_name(amp_slot()), message[6]));
    uint8_t rename[] = {8, 0, 0xca, 3, 9, 10, 5, 'N', 'e', 'w', ' ', ' ', 16, 2};
    request(rename, sizeof(rename));
    assert(storage.kind == S_RENAME && storage.from == 2 && !strcmp(storage.name, "New") && !response());
    storage_busy = false; storage_success = false; tone_tick();
    assert(response() && fields[0] == 200);
    rename[2] = 0xb2;
    request(rename, sizeof(rename));
    assert(storage.kind == S_SAVE && storage.to == 2 && !strcmp(storage.name, "New") && !response());
    storage_busy = false; tone_tick();
    assert(response() && fields[0] == 200);
    move[8] = 25;
    request(move, sizeof(move));
    assert(response() && fields[0] == 200 && !storage_busy);
    tone_reset();
}

static void tuner(void) {
    tone_reset();
    uint8_t mode[] = {8, 0, 0x8a, 7, 4, 8, 6, 16, 0};
    for (unsigned context = 6; context <= 7; context++) {
        mode[6] = context;
        request(mode, sizeof(mode));
        assert(tuner_busy && tuner_enabled == (context == 6) && !response());
        tone_tick(); assert(!response());
        tuner_busy = false; tone_tick();
        assert(response() && replies == 1 && fields[0] == 113 && acks[0] == 2);
        assert(message[6] == context && message[8] == 0);
    }
    mode[6] = 8;
    request(mode, sizeof(mode));
    assert(response() && fields[0] == 200 && !tuner_busy);
    mode[6] = 6; connected = false;
    request(mode, sizeof(mode));
    assert(response() && fields[0] == 200 && !tuner_busy);
    connected = true;
    tone_reset();
}

int main(void) {
    storage_commands(); tuner();
    static const uint8_t status[] = {8, 0, 0xc2, 6, 2, 8, 1};
    static const uint8_t gain[] = {
        8, 0, 0x92, 2, 16, 10, 3, 'a', 'm', 'p', 18, 4,
        'g', 'a', 'i', 'n', 29, 0, 0, 0x80, 0x3f,
    };

    // Status queries use the current cache.
    request(status, sizeof(status));
    size_t size = response();
    assert(size && message[1] == 2 && contains(size, "\"gain\":0.2"));
    assert(contains(size, "\"displayName\":\"Beauty Clean\""));
    assert(!contains(size, "TORINO 14"));
    assert(!sets);

    // Slot queries use the requested name, not the selected slot's name.
    uint8_t get_slot[] = {8, 0, 0xca, 6, 2, 8, 1};
    request(get_slot, sizeof(get_slot));
    size = response();
    assert(size && fields[0] == 31 && contains(size, "\"displayName\":\"Brutal Metal II\""));
    get_slot[6] = 60;
    request(get_slot, sizeof(get_slot));
    size = response();
    assert(size && contains(size, "\"displayName\":\"TORINO 60\""));

    // Names cannot break JSON, even with control bytes or no wire terminator.
    stored_name = "A\"\\\n\177";
    request(status, sizeof(status));
    size = response();
    assert(size && contains(size, "\"displayName\":\"A\\\"\\\\\\u000a\\u007f\""));
    char longest[33];
    memset(longest, 1, 32); longest[32] = 0;
    stored_name = longest;
    request(status, sizeof(status));
    size = response();
    assert(size && contains(size, "\\u0001\\u0001"));
    stored_name = "Beauty Clean";

    // All live edits enter the cache immediately.
    request(gain, sizeof(gain));
    assert(sets == 1 && last.number == 1 && response());
    assert(replies == 2 && fields[0] == 35 && fields[1] == 38 && acks[0] == 2);
    cached = 255;
    request(status, sizeof(status));
    size = response();
    assert(size && contains(size, "\"gain\":1,"));

    static const uint8_t bypass[] = {8, 0, 0x92, 2, 17, 10, 5, 's','t','o','m','p',
        18, 6, 'b','y','p','a','s','s', 48, 1};
    request(bypass, sizeof(bypass));
    assert(last.type == C_BOOL && last.number == 1 && response());
    assert(fields[0] == 35 && acks[0] == 2);

    static const uint8_t sag[] = {8, 0, 0x92, 2, 16, 10, 3, 'a','m','p',
        18, 3, 's','a','g', 34, 4, 'm','o','r','e'};
    request(sag, sizeof(sag));
    assert(last.type == C_STRING && !strcmp(last.text, "more") && response());

    static const uint8_t model[] = {8, 0, 0xba, 2, 20, 10, 3, 'a','m','p',
        18, 13, 'D','U','B','S','_','D','e','l','u','x','e','6','5'};
    request(model, sizeof(model));
    assert(last.replace && response());
    assert(replies == 3 && fields[0] == 40 && fields[1] == 36 && fields[2] == 38);
    assert(acks[0] == 2 && acks[1] == 0);

    // Queries and connection checks never wait for amplifier completion.
    static const uint8_t check[] = {8, 0, 0xd2, 12, 2, 8, 1};
    request(check, sizeof(check));
    assert(response() && fields[0] == 203 && acks[0] == 2);
    request(status, sizeof(status));
    assert(response());

    // Physical updates use unsolicited status, not a response to an old request.
    cached = 128;
    tone_notify(1, false); tone_tick();
    assert(response() && replies == 2 && fields[0] == 36 && acks[0] == 0);
    assert(fields[1] == 38);
    // Busy transport collapses repeated physical changes into the latest value.
    cached = 150; tone_notify(1, false);
    cached = 200; tone_notify(1, false);
    cached = 255; tone_notify(1, false);
    tone_tick();
    assert(response() && replies == 2 && fields[0] == 36);
    // Model changes use ReplaceNodeStatus, never DspUnitParameterStatus.
    physical_model = 246;
    tone_notify(1, false); tone_tick();
    size = response();
    assert(size && fields[0] == 40 && acks[0] == 0 && contains(size, "DUBS_Twin57"));
    physical_model = 117;
    tone_notify(31, true); tone_tick();
    assert(response() && fields[0] == 31 && fields[1] == 37 && fields[2] == 32 && acks[0] == 0);

    // Transport reset does not replay or undo accepted edits.
    unsigned before = sets;
    tone_reset(); tone_tick();
    assert(!response() && sets == before);
    connected = false;
    request(status, sizeof(status));
    assert(response() && fields[0] == 200);
    connected = true;
    valid = false;
    request(gain, sizeof(gain));
    assert(response() && fields[0] == 200 && sets == before);
    valid = true;

    // Malformed typed values never reach the controller.
    uint8_t invalid[sizeof(bypass)]; memcpy(invalid, bypass, sizeof(invalid));
    invalid[sizeof(invalid) - 1] = 2;
    request(invalid, sizeof(invalid));
    assert(response() && sets == before);
    knob("amp", "gain", .7f);
    assert(response() && sets == before + 1 && last.number == .7f);
    // Preset switches send one full snapshot, never redundant parameter updates.
    tone_reset();
    snapshot_override = true;
    physical_snapshot = (control_state_t){.valid = true};
    const uint8_t ids[] = {117, 186, 19, 22, 38};
    const uint8_t positions[] = {0, 0, 1, 4, 5};
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        physical_snapshot.raw[m][16] = ids[m];
        physical_snapshot.raw[m][18] = positions[m];
    }
    memcpy(physical_snapshot.latent, physical_snapshot.raw, sizeof(physical_snapshot.raw));
    tone_notify(31, true); tone_tick();
    assert(response() && !stops && replies == 4);
    assert(fields[0] == 31 && fields[1] == 37 && fields[2] == 32 && fields[3] == 38);
    assert(acks[0] == 0 && acks[1] == 0 && acks[2] == 0);

    // The full snapshot advances the baseline for later physical knob changes.
    tone_notify(31, false); tone_tick();
    assert(response() && replies == 1 && fields[0] == 38);
    physical_snapshot.raw[0][33] = 128;
    tone_notify(1, false); tone_tick();
    assert(response() && replies == 2 && fields[0] == 36 && fields[1] == 38);

    // Consecutive switches still replace the whole snapshot without deltas.
    physical_snapshot.raw[0][16] = 246;
    physical_snapshot.raw[4][18] = 0;
    tone_notify(31, true); tone_tick();
    size = response();
    assert(size && replies == 4 && fields[0] == 31 && fields[1] == 37 && fields[2] == 32 && fields[3] == 38);
    assert(contains(size, "DUBS_Twin57"));
    // The secondary selector crosses models and empty slots without UI-only messages.
    physical_snapshot.raw[3][16] = 43;
    size = effect_update(3);
    assert(contains(size, "DUBS_TapeDelayLite"));
    physical_snapshot.raw[3][16] = 0;
    size = effect_update(3);
    assert(contains(size, "\"bypass\":true"));
    physical_snapshot.raw[3][16] = 43;
    effect_update(3);

    // Bypass and placement refresh even when the visible model stays unchanged.
    memcpy(physical_snapshot.latent[3], physical_snapshot.raw[3], 64);
    physical_snapshot.raw[3][16] = 0;
    effect_update(3);
    physical_snapshot.raw[3][16] = 43;
    effect_update(3);
    physical_snapshot.raw[3][18] = 1;
    effect_update(3);

    // Rapid selector changes collapse to the latest complete state.
    physical_snapshot.raw[2][16] = 0;
    tone_notify(1u << 2, false);
    physical_snapshot.raw[2][16] = 19;
    physical_snapshot.raw[3][16] = 22;
    size = effect_update(3);
    assert(!contains(size, "DUBS_TapeDelayLite"));
    tone_notify(31, false); tone_tick();
    assert(response() && replies == 1 && fields[0] == 38);

    // Ordinary effect knobs still use the small UI parameter update path.
    physical_snapshot.raw[2][33] = 128;
    tone_notify(1u << 2, false); tone_tick();
    assert(response() && replies == 2 && fields[0] == 36 && fields[1] == 38);
    // Fuzz uses the LT model name and reports Vari changes as string parameters.
    physical_snapshot.raw[1][16] = 26;
    physical_snapshot.raw[1][17] = 0;
    physical_snapshot.raw[1][35] = 128;
    size = effect_update(1);
    assert(contains(size, "DUBS_VariFuzz") && contains(size, "\"tone\":\"loose\""));
    physical_snapshot.raw[1][35] = 29;
    tone_notify(1u << 1, false); tone_tick();
    size = response();
    assert(size && replies == 2 && fields[0] == 36 && fields[1] == 38);
    assert(contains(size, "tone") && contains(size, "normal"));

    // Sustain exposes Compressor 7 through LT's two visible controls.
    physical_snapshot.raw[1][16] = 7;
    physical_snapshot.raw[1][34] = 1;
    size = effect_update(1);
    assert(contains(size, "DUBS_Sustain") && contains(size, "\"sensitivity\":"));
    assert(contains(size, "\"outputLevel\":"));
    physical_snapshot.raw[1][34] = 2;
    tone_notify(1u << 1, false); tone_tick();
    size = response();
    assert(size && replies == 2 && fields[0] == 36 && fields[1] == 38);
    assert(contains(size, "sensitivity"));

    // Metal Gate acknowledges Mustang's selected step, not an unsupported LT value.
    physical_snapshot.gate = (control_gate_t){.selected = true, .threshold = 9};
    memset(physical_snapshot.raw[1] + 16, 0, 48);
    memset(physical_snapshot.latent[1] + 16, 0, 48);
    knob("stomp", "threshold", -30);
    size = response();
    assert(size && fields[0] == 35 && fields[1] == 38);
    const uint8_t capped[] = {29, 0, 0, 0x28, 0xc2}; // float -42, little endian.
    bool found = false;
    for (size_t i = 0; i + sizeof(capped) <= size; i++)
        if (!memcmp(message + i, capped, sizeof(capped))) found = true;
    assert(found);

    // Physical gate bypass uses a full update, never the crashing UI parameter path.
    physical_snapshot.gate.bypassed = true;
    tone_notify(2, false); tone_tick();
    size = response();
    assert(size && replies == 2 && fields[0] == 32 && fields[1] == 38);
    assert(contains(size, "DUBS_ChromeGate") && contains(size, "\"threshold\":-42"));
    preset_loads();
    puts("Tone: typed controls, preset selection, factory preview/exit, safe selectors, reconnect passed");
}
