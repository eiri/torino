#pragma once
#include "probe.h"

#define CONTROL_MODULES 5
#define CONTROL_VALUES 16

enum control_type { C_FLOAT = 3, C_STRING, C_INT, C_BOOL };
typedef struct {
    char node[16], parameter[32], text[48];
    enum control_type type;
    float number;
    bool replace;
} control_request_t;

typedef struct {
    bool selected, bypassed;
    uint8_t threshold, saved[3];
} control_gate_t;

typedef struct {
    uint8_t raw[CONTROL_MODULES][64], latent[CONTROL_MODULES][64];
    control_gate_t gate;
    unsigned index[CONTROL_MODULES];
    uint16_t available[CONTROL_MODULES][29];
    uint8_t available_count[CONTROL_MODULES];
    bool valid;
} control_state_t;

typedef struct {
    unsigned module, count;
    uint8_t expected[64], latent[64], packets[2][64];
    bool gate_update;
    control_gate_t gate;
    uint8_t amp_gate[3];
} control_plan_t;

typedef struct {
    uint8_t raw[CONTROL_MODULES][64], mask, slot;
    bool preset;
} control_event_t;

enum control_event { E_BAD = -1, E_NONE, E_WAIT, E_CHANGED };
enum control_event controls_event(control_state_t *state, control_event_t *event,
                                 const uint8_t *packet, size_t length, unsigned *changed);
const char *controls_model(const control_state_t *state, unsigned module);
bool controls_adopt(control_state_t *state, const probe_t *capture);
bool controls_plan(const control_state_t *state, const control_request_t *request,
                   control_plan_t *plan);
void controls_commit(control_state_t *state, const control_plan_t *plan);
void controls_accept(control_state_t *state, const control_plan_t *plan);
bool controls_diff(const control_state_t *wanted, const control_state_t *actual,
                   unsigned module, control_plan_t *plan);
size_t controls_json(const control_state_t *state, char *json, size_t size);
size_t controls_values(const control_state_t *state, const char *node,
                       control_request_t values[CONTROL_VALUES]);
const char *controls_node(unsigned module);
bool controls_path(const control_state_t *state, const char *path[7]);
