#include "store.h"
#include <string.h>

enum phase { ST_IDLE, ST_BEGIN, ST_READ, ST_CAPTURE, ST_APPLY, ST_SAVE,
    ST_VERIFY, ST_CHECK, ST_RETURN, ST_RESTORE, ST_FINISH };
static enum phase phase;
static store_request_t request;
static store_preset_t presets[24], original, expected;
static control_state_t before;
static unsigned slots[24], source[24], total, cursor, return_slot;
static bool success;
static store_preset_t cached[24];
static bool valid[24], changed[24];

bool store_changed(unsigned slot) { return slot >= 1 && slot <= 24 && changed[slot - 1]; }
bool store_snapshot(unsigned slot, control_state_t *state, char name[33]) {
    if (slot < 1 || slot > 24 || !valid[slot - 1]) return false;
    store_state(state);
    state->gate = (control_gate_t){0};
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        memcpy(state->raw[m], cached[slot - 1].raw[m + 1], 64);
        memcpy(state->latent[m], state->raw[m], 64);
    }
    memcpy(name, cached[slot - 1].raw[0] + 16, 32); name[32] = 0;
    return true;
}

bool store_busy(void) { return phase != ST_IDLE; }
bool store_ok(void) { return success; }

bool store_start(const store_request_t *r) {
    if (store_busy() || !store_online() || (unsigned)r->kind > S_MOVE ||
        r->from < 1 || r->from > TORINO_STORE_LIMIT || r->to < 1 || r->to > TORINO_STORE_LIMIT ||
        !memchr(r->name, 0, sizeof(r->name))) return false;
    request = *r;
    success = false;
    memset(changed, 0, sizeof(changed));
    phase = ST_BEGIN;
    return true;
}

static void fail(void) {
    phase = ST_IDLE;
    success = false;
    store_fault();
}

static bool same(const store_preset_t *a, const store_preset_t *b) {
    if (memcmp(a->raw[0] + 16, b->raw[0] + 16, 20)) return false;
    for (unsigned m = 1; m <= CONTROL_MODULES; m++)
        if (memcmp(a->raw[m] + 16, b->raw[m] + 16, 48)) return false;
    return a->raw[6][16] == b->raw[6][16];
}

static void plan(void) {
    total = 0;
    return_slot = store_slot();
    if (request.kind == S_SAVE) {
        slots[total++] = request.to;
        if (request.load) return_slot = request.to;
    } else if (request.kind == S_RENAME) {
        slots[total++] = request.from;
    } else if (request.kind == S_SWAP) {
        slots[total++] = request.from;
        if (request.to != request.from) slots[total++] = request.to;
        if (return_slot == request.from) return_slot = request.to;
        else if (return_slot == request.to) return_slot = request.from;
    } else {
        unsigned low = request.from < request.to ? request.from : request.to;
        unsigned high = request.from > request.to ? request.from : request.to;
        for (unsigned s = low; s <= high; s++) slots[total++] = s;
        if (return_slot == request.from) return_slot = request.to;
        else if (request.from < request.to && return_slot > request.from && return_slot <= request.to) return_slot--;
        else if (request.from > request.to && return_slot >= request.to && return_slot < request.from) return_slot++;
    }
    for (unsigned i = 0; i < total; i++) {
        source[i] = i;
        unsigned wanted = slots[i];
        if (request.kind == S_SWAP) wanted = slots[total - 1 - i];
        if (request.kind == S_MOVE) {
            if (wanted == request.to) wanted = request.from;
            else wanted = request.from < request.to ? wanted + 1 : wanted - 1;
        }
        for (unsigned j = 0; j < total; j++) if (slots[j] == wanted) source[i] = j;
    }
    cursor = 0;
}

void store_tick(void) {
    if (!store_busy()) return;
    if (!store_online()) { fail(); return; }
    if (!store_idle()) return;
    switch (phase) {
    case ST_BEGIN:
        store_state(&before);
        if (!store_get(&original)) { fail(); return; }
        plan();
        if (request.kind == S_SAVE) {
            presets[0] = original;
            if (request.draft) {
                for (unsigned m = 0; m < CONTROL_MODULES; m++)
                    memcpy(presets[0].raw[m + 1], request.state.raw[m], 64);
            }
            phase = ST_APPLY;
        } else phase = ST_READ;
        return;
    case ST_READ:
        if (!store_load(slots[cursor])) { fail(); return; }
        phase = ST_CAPTURE;
        return;
    case ST_CAPTURE:
        if (!store_get(&presets[cursor])) { fail(); return; }
        cached[slots[cursor] - 1] = presets[cursor]; valid[slots[cursor] - 1] = true;
        if (++cursor == total) { cursor = 0; phase = ST_APPLY; }
        else phase = ST_READ;
        return;
    case ST_APPLY:
        expected = presets[source[cursor]];
        if (request.kind == S_SAVE || request.kind == S_RENAME) {
            memset(expected.raw[0] + 16, 0, 32);
            memcpy(expected.raw[0] + 16, request.name, strlen(request.name));
        }
        if (!store_apply(&expected)) { fail(); return; }
        phase = ST_SAVE;
        return;
    case ST_SAVE: {
        char name[20];
        memcpy(name, expected.raw[0] + 16, 19); name[19] = 0;
        memset(expected.raw[0] + 16, 0, 32);
        memcpy(expected.raw[0] + 16, name, strlen(name));
        if (!store_save(slots[cursor], name)) { fail(); return; }
        phase = ST_VERIFY;
        return;
    }
    case ST_VERIFY:
        if (!store_load(slots[cursor])) { fail(); return; }
        phase = ST_CHECK;
        return;
    case ST_CHECK: {
        static store_preset_t readback;
        if (!store_get(&readback) || !same(&expected, &readback)) { fail(); return; }
        cached[slots[cursor] - 1] = readback;
        valid[slots[cursor] - 1] = changed[slots[cursor] - 1] = true;
        phase = ++cursor == total ? ST_RETURN : ST_APPLY;
        return;
    }
    case ST_RETURN:
        if (!store_load(return_slot)) { fail(); return; }
        phase = ST_RESTORE;
        return;
    case ST_RESTORE:
        if (!(request.kind == S_SAVE && request.load) &&
            !store_restore(&before, original.raw[6][16])) { fail(); return; }
        phase = ST_FINISH;
        return;
    case ST_FINISH:
        success = true;
        phase = ST_IDLE;
        return;
    default: fail(); return;
    }
}
