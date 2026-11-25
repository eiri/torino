#include "trace.h"
#include <stdio.h>
#include <string.h>

static struct {
    uint32_t ms;
    unsigned sequence;
    uint8_t data[64], length;
    bool output;
} events[256];
static unsigned head, count, sequence, dropped;
static bool enabled;

void trace_enable(bool value) {
    enabled = value;
    if (!value) head = count = 0;
}

void trace_record(bool output, uint32_t ms, const uint8_t *data, size_t length) {
    if (!enabled) return;
    unsigned next = ++sequence;
    if (!data || length > 64 || count == 256) { dropped++; return; }
    unsigned index = (head + count++) % 256;
    events[index].ms = ms;
    events[index].sequence = next;
    events[index].output = output;
    events[index].length = length;
    memcpy(events[index].data, data, length);
}

size_t trace_line(char *line, size_t size) {
    if (!count || size < TRACE_LINE) return 0;
    unsigned index = head;
    int n = snprintf(line, size, "USB seq=%u ms=%lu dir=%s len=%u data=",
        events[index].sequence, (unsigned long)events[index].ms,
        events[index].output ? "TONE>BOARD" : "BOARD>TONE", events[index].length);
    if (n < 0 || (size_t)n + 2u * events[index].length + 1u >= size) return 0;
    size_t used = n;
    static const char hex[] = "0123456789abcdef";
    for (unsigned i = 0; i < events[index].length; i++) {
        line[used++] = hex[events[index].data[i] >> 4];
        line[used++] = hex[events[index].data[i] & 15];
    }
    line[used++] = '\n'; line[used] = 0;
    head = (head + 1) % 256; count--;
    return used;
}

unsigned trace_lost(void) { return dropped; }
