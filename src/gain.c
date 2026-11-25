#include "gain.h"
#include <string.h>

bool gain_batch(gain_t *g, const uint8_t packets[2][64], unsigned count) {
    if (g->spent || g->failed) return false;
    g->spent = true;
    if (!count || count > 2 || (count == 2 &&
        (packets[0][2] == 5 || packets[0][2] != packets[1][2] || packets[0][16] || packets[0][17]))) {
        g->failed = true; g->error = "control-plan"; return false;
    }
    for (unsigned i = 0; i < count; i++) {
        uint8_t header[16] = {0x1c, 3, packets[i][2], 0, 0, 0, 1, 1};
        if (packets[i][2] < 5 || packets[i][2] > 9 || memcmp(packets[i], header, 16)) {
            g->failed = true; g->error = "control-header"; return false;
        }
    }
    g->count = count;
    memcpy(g->data, packets[0], 64);
    if (count == 2) memcpy(g->extra, packets[1], 64);
    return true;
}

bool gain_select(gain_t *g, unsigned slot) {
    if (g->spent || g->failed || slot >= 24) return false;

    *g = (gain_t){.spent = true, .selection = true, .count = 1,
        .data = {0x1c, 0xc3, 1, 0, (uint8_t)slot, 0, 1, 0}};
    return true;
}

bool gain_tuner(gain_t *g, bool enabled) {
    if (g->spent || g->failed) return false;
    *g = (gain_t){.spent = true, .selection = true, .count = 1,
        .data = {10, 1, enabled, enabled, enabled}};
    return true;
}

bool gain_save(gain_t *g, unsigned slot, const char *name) {
    if (g->spent || g->failed || slot >= 24 || !name) return false;
    size_t n = 0;
    while (n < 20 && name[n]) {
        if ((unsigned char)name[n] < 32 || (unsigned char)name[n] > 126) return false;
        n++;
    }
    if (n == 20) return false;
    *g = (gain_t){.spent = true, .count = 1,
        .data = {0x1c, 0xc3, 3, 0, slot, 0, 1, 1}};
    memcpy(g->data + 16, name, n);
    return true;
}

bool gain_expression(gain_t *g, uint8_t value) {
    if (g->spent || g->failed) return false;
    *g = (gain_t){.spent = true, .count = 1,
        .data = {0x1c, 3, 13, 0, 0, 0, 1, 1}};
    g->data[16] = value;
    return true;
}

unsigned gain_steps(const gain_t *g) { return g->selection ? 1 : g->count * 2; }

bool gain_next(gain_t *g, uint8_t packet[64]) {
    if (!g->spent || g->failed || g->pending || g->completed >= gain_steps(g)) return false;
    if (!(g->completed & 1)) {
        memcpy(packet, g->completed ? g->extra : g->data, 64);
    } else {
        memset(packet, 0, 64);
        packet[0] = 0x1c;
        packet[1] = 3;
    }
    g->pending = true;
    g->sent = g->acked = false;
    return true;
}

static void advance(gain_t *g) {
    if (g->sent && g->acked) {
        g->pending = false;
        g->completed++;
    }
}

bool gain_sent(gain_t *g, const uint8_t *packet, size_t length) {
    uint8_t expected[64] = {0x1c, 3};
    if (!(g->completed & 1)) memcpy(expected, g->completed ? g->extra : g->data, 64);
    if (!g->pending || g->sent || g->failed || g->completed >= gain_steps(g) || length != 64 ||
        !packet || memcmp(packet, expected, 64)) {
        g->failed = true;
        g->error = "gain-send-completion";
        return false;
    }
    g->sent = true;
    advance(g);
    return true;
}

bool gain_receive(gain_t *g, const uint8_t *packet, size_t length) {
    if (!g->pending || g->acked || g->failed || !packet || length != 64 ||
        (packet[0] == 0x1c && packet[1] == 1)) {
        g->failed = true;
        g->error = "gain-reply";
        return false;
    }
    g->acked = true;
    advance(g);
    return true;
}
