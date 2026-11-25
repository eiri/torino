#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TRACE_LINE 192u

void trace_enable(bool enabled);
void trace_record(bool output, uint32_t ms, const uint8_t *data, size_t length);
size_t trace_line(char *line, size_t size);
unsigned trace_lost(void);
