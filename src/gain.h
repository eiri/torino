#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool spent, pending, failed, sent, acked, selection;
    unsigned completed;
    const char *error;
    uint8_t data[64], extra[64];
    unsigned count;
} gain_t;

bool gain_batch(gain_t *gain, const uint8_t packets[2][64], unsigned count);
bool gain_select(gain_t *gain, unsigned slot);
bool gain_tuner(gain_t *gain, bool enabled);
bool gain_save(gain_t *gain, unsigned slot, const char *name);
bool gain_expression(gain_t *gain, uint8_t value);
unsigned gain_steps(const gain_t *gain);
bool gain_next(gain_t *gain, uint8_t packet[64]);
bool gain_sent(gain_t *gain, const uint8_t *packet, size_t length);
bool gain_receive(gain_t *gain, const uint8_t *packet, size_t length);
