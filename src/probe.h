#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PROBE_REPORTS 148
#define PROBE_SIZE 64

enum probe_phase { P_IDLE, P_HELLO, P_INIT, P_SETTINGS, P_DONE, P_FAILED };

typedef struct {
    enum probe_phase phase;
    uint16_t count;
    uint8_t writes;
    bool pending;
    const char *error;
    uint8_t reports[PROBE_REPORTS][PROBE_SIZE];
} probe_t;

bool probe_target(uint16_t vid, uint16_t pid, uint8_t interface,
                  const uint8_t *descriptor, size_t length);
bool probe_start(probe_t *probe);
bool probe_refresh(probe_t *probe, const probe_t *baseline);
bool probe_selected(probe_t *out, const probe_t *stream, const probe_t *baseline);
bool probe_next(probe_t *probe, uint8_t packet[PROBE_SIZE]);
void probe_sent(probe_t *probe, size_t length);
void probe_receive(probe_t *probe, const uint8_t *packet, size_t length);
void probe_fail(probe_t *probe, const char *reason);
