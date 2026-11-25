#pragma once
#include "controls.h"

#ifndef TORINO_STORE_LIMIT
#define TORINO_STORE_LIMIT 24
#endif
_Static_assert(TORINO_STORE_LIMIT >= 2 && TORINO_STORE_LIMIT <= 24, "storage slot limit");

enum store_kind { S_SAVE, S_RENAME, S_SWAP, S_MOVE };
typedef struct {
    enum store_kind kind;
    unsigned from, to;
    char name[20];
    bool draft, load;
    control_state_t state;
} store_request_t;
typedef struct { uint8_t raw[8][64]; } store_preset_t;

bool store_start(const store_request_t *request);
bool store_busy(void);
bool store_ok(void);
void store_tick(void);
bool store_snapshot(unsigned slot, control_state_t *state, char name[33]);
bool store_changed(unsigned slot);

bool store_idle(void);
bool store_online(void);
unsigned store_slot(void);
void store_state(control_state_t *state);
bool store_get(store_preset_t *preset);
bool store_load(unsigned slot);
bool store_apply(const store_preset_t *preset);
bool store_restore(const control_state_t *state, uint8_t expression);
bool store_save(unsigned slot, const char *name);
void store_fault(void);
