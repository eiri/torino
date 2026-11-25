#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define NATIVE_LIMIT 32u

typedef struct {
    atomic_uint written, consumed;
    uint8_t reports[NATIVE_LIMIT][64];
} native_queue_t;

bool native_push(native_queue_t *queue, const uint8_t *packet, size_t length);
bool native_pop(native_queue_t *queue, uint8_t packet[64]);
bool native_notice(const uint8_t *packet, size_t length);
