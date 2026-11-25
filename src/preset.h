#pragma once
#include "controls.h"

bool preset_decode(const char *json, size_t size, const control_state_t *base,
                   control_state_t *draft, char name[33]);
