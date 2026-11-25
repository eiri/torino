#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "controls.h"

void tone_receive(const uint8_t *data, size_t length);
bool tone_next(uint8_t report[64]);
void tone_sent(void);
void tone_tick(void);
void tone_reset(void);
void tone_notify(unsigned modules, bool preset);
void amp_snapshot(control_state_t *state);
size_t tone_diag(char *text, size_t size);

bool amp_connected(void);
void amp_stop(void);
bool amp_control(const control_request_t *request);
bool amp_select(unsigned slot);
bool amp_loading(void);
bool amp_tuner(bool enabled);
bool amp_tuning(void);
bool amp_apply(const control_state_t *state);
size_t amp_nodes(char *json, size_t size);
size_t amp_values(const char *node, control_request_t values[CONTROL_VALUES]);
bool amp_dirty(void);
bool amp_path(const char *path[7]);
unsigned amp_slot(void);
const char *amp_name(unsigned slot);
