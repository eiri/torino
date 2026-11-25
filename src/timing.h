#pragma once
#include <stdbool.h>
#include <stdint.h>

static inline uint8_t host_interval(uint16_t vid, uint16_t pid, bool full_speed,
                                    uint8_t address, uint8_t type, uint16_t size, uint8_t interval) {
    return vid == 0x1ed8 && pid == 0x0014 && full_speed && (address & 0x80) &&
           type == 3 && size == 64 && interval ? 1 : interval;
}
