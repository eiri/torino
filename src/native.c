#include "native.h"
#include <string.h>

bool native_notice(const uint8_t *packet, size_t length) {
    return packet && length == 64 && packet[0] == 5 && packet[1] == 1 && packet[2] == 2;
}

bool native_push(native_queue_t *q, const uint8_t *packet, size_t length) {
    if (!packet || length != 64) return false;
    unsigned written = atomic_load_explicit(&q->written, memory_order_relaxed);
    if (written - atomic_load_explicit(&q->consumed, memory_order_acquire) >= NATIVE_LIMIT) return false;
    memcpy(q->reports[written % NATIVE_LIMIT], packet, 64);
    atomic_store_explicit(&q->written, written + 1, memory_order_release);
    return true;
}

bool native_pop(native_queue_t *q, uint8_t packet[64]) {
    unsigned consumed = atomic_load_explicit(&q->consumed, memory_order_relaxed);
    if (consumed == atomic_load_explicit(&q->written, memory_order_acquire)) return false;
    memcpy(packet, q->reports[consumed % NATIVE_LIMIT], 64);
    atomic_store_explicit(&q->consumed, consumed + 1, memory_order_release);
    return true;
}
