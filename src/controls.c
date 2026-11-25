#include "controls.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    uint8_t offset, type;
    float low, high;
    const char *choices;
} param_t;
typedef struct {
    const char *name;
    unsigned module;
    uint16_t id;
    uint8_t layout, reserved, fixed[3], defaults[23];
    const param_t *params;
    size_t count;
    uint16_t ui_mask;
} model_t;

enum { P_LINEAR, P_DB, P_ENUM, P_BOOL, P_BLEND, P_VARI, P_SENSE };

static const uint8_t vari_low[] = {0, 29, 128};
#define NUM(n, o) {n, o, P_LINEAR, 0, 1, NULL}
#define RANGE(n, o, l, h) {n, o, P_LINEAR, l, h, NULL}
#define DB(n, o, l, h) {n, o, P_DB, l, h, NULL}
#define ENUM(n, o, c) {n, o, P_ENUM, 0, 0, c}
#define BOOL(n, o) {n, o, P_BOOL, 0, 1, NULL}

static const param_t drive_params[] = {
    NUM("level", 32), NUM("gain", 33), NUM("low", 34), NUM("mid", 35), NUM("high", 36),
};
static const param_t green_params[] = {
    DB("level", 32, -27, 0), NUM("gain", 33), NUM("tone", 34),
    {"blend", 35, P_BLEND, 0, 1, NULL},
};
static const param_t black_params[] = {
    DB("level", 32, -40, -7), NUM("gain", 33), NUM("tone", 34),
};
static const param_t fuzz_params[] = {
    DB("level", 32, -40, -8), NUM("tone", 33), NUM("gain", 34),
};
static const param_t vari_params[] = {
    NUM("level", 32), NUM("gain", 33),
    {"tone", 35, P_VARI, 0, 0, "tight|normal|loose"},
};
static const param_t comp_params[] = {
    ENUM("type", 32, "low|medium|high|super"),
};
static const param_t sustain_params[] = {
    {"sensitivity", 34, P_SENSE, 0, 1, NULL}, NUM("outputLevel", 32),
};
static const param_t chorus_params[] = {
    NUM("level", 32), RANGE("rateHz", 33, .08f, 10), NUM("depth", 34),
    NUM("avgDelay", 35), NUM("lrPhase", 36),
};
static const param_t flanger_params[] = {
    NUM("level", 32), RANGE("rate", 33, .08f, 10), NUM("depth", 34),
    NUM("feedback", 35), NUM("phase", 36),
};
static const param_t rotor_params[] = {
    NUM("level", 32), RANGE("rotor", 33, .67f, 5.67f), NUM("depth", 34),
    NUM("feedback", 35), NUM("phase", 36),
};
static const param_t trem_params[] = {
    NUM("level", 32), RANGE("rate", 33, 1.3f, 10), NUM("duty", 34),
    NUM("dist", 35), NUM("shape", 36),
};
static const param_t phaser_params[] = {
    NUM("level", 32), RANGE("rate", 33, .08f, 10), NUM("depth", 34),
    NUM("feedback", 35), ENUM("shape", 36, "sine|triangle"),
};
static const param_t filter_params[] = {
    NUM("level", 32), RANGE("rate", 33, .08f, 10), NUM("reson", 34),
    NUM("loFrq", 35), NUM("hiFrq", 36),
};
static const param_t wah_params[] = {
    NUM("level", 32),
};
static const param_t reverse_params[] = {
    NUM("level", 32), RANGE("time", 33, .03f, 1.11f),
};
static const param_t delay_params[] = {
    NUM("level", 32), RANGE("time", 33, .03f, 1.11f), NUM("feedback", 34),
    NUM("brite", 35), NUM("attenuate", 36),
};
static const param_t tape_params[] = {
    NUM("wetLvl", 32), RANGE("dlyTime", 33, .03f, 1.11f),
    NUM("feedback", 34), NUM("wowLevel", 35),
};
static const param_t reverb_params[] = {
    NUM("level", 32), NUM("decay", 33), NUM("dwell", 34), NUM("diffuse", 35), NUM("tone", 36),
};
static const param_t spring_params[] = {
    NUM("wetLvl", 32), NUM("decay", 33), NUM("dwell", 34), NUM("diffuse", 35), NUM("tone", 36),
};

#include "controls_data.h"

static const char *const nodes[] = {"amp", "stomp", "mod", "delay", "reverb"};
const char *controls_node(unsigned module) {
    return module < CONTROL_MODULES ? nodes[module] : NULL;
}

static uint16_t model_id(const uint8_t raw[64]) {
    return raw[16] | (uint16_t)raw[17] << 8;
}

static const model_t *find_model(unsigned module, uint16_t id, const char *name) {
    for (size_t i = 0; i < sizeof(models) / sizeof(models[0]); i++) {
        const model_t *m = &models[i];
        if (m->module == module && (name ? !strcmp(m->name, name) : m->id == id)) return m;
    }
    return NULL;
}

static bool choice(const char *list, unsigned index, char *out, size_t size) {
    while (index--) {
        list = strchr(list, '|');
        if (!list) return false;
        list++;
    }
    const char *end = strchr(list, '|');
    size_t n = end ? (size_t)(end - list) : strlen(list);
    if (n >= size) return false;
    memcpy(out, list, n); out[n] = 0;
    return true;
}

static bool decode(const param_t *p, uint8_t v, control_request_t *r) {
    snprintf(r->parameter, sizeof(r->parameter), "%s", p->name);
    if (p->type == P_ENUM || p->type == P_VARI) {
        if (p->type == P_VARI) {
            unsigned best = 0, distance = 256;
            for (unsigned i = 0; i < sizeof(vari_low); i++) {
                unsigned delta = v > vari_low[i] ? v - vari_low[i] : vari_low[i] - v;
                if (delta < distance) { best = i; distance = delta; }
            }
            v = best;
        }
        r->type = C_STRING;
        return choice(p->choices, v, r->text, sizeof(r->text));
    }
    if (p->type == P_BOOL) {
        r->type = C_BOOL; r->number = v;
        return v <= 1;
    }
    r->type = C_FLOAT;
    double value = p->low + (p->high - p->low) * (double)v / 255;
    if (p->type == P_DB) {
        double floor = pow(10, (p->low - p->high) / 20);
        value = p->high + 20 * log10(floor + (1 - floor) * (double)v / 255);
    }
    if (p->type == P_BLEND) value = fabs(2 * (double)v / 255 - 1);
    if (p->type == P_SENSE) {
        double ratio = 2 + 998 * (double)v / 255;
        value = fmin(1, ratio <= 4 ? (ratio - 2) / 4 : .5 + (ratio - 4) / 12);
    }
    r->number = (float)value;
    return true;
}

static bool encode(const param_t *p, const control_request_t *r, uint8_t *value) {
    if (p->type == P_ENUM || p->type == P_VARI) {
        if (r->type != C_STRING) return false;
        char item[48];
        for (unsigned i = 0; choice(p->choices, i, item, sizeof(item)); i++) {
            if (!strcmp(item, r->text)) {
                *value = p->type == P_VARI ? vari_low[i] : i;
                return true;
            }
        }
        return false;
    }
    if (p->type == P_BOOL) {
        if (r->type != C_BOOL || (r->number != 0 && r->number != 1)) return false;
        *value = (uint8_t)r->number;
        return true;
    }
    if (r->type != C_FLOAT || !isfinite(r->number) || r->number < p->low || r->number > p->high) return false;
    float scaled = (r->number - p->low) / (p->high - p->low);
    if (p->type == P_DB) {
        float floor = powf(10, (p->low - p->high) / 20);
        scaled = (powf(10, (r->number - p->high) / 20) - floor) / (1 - floor);
    }
    if (p->type == P_BLEND) scaled = (r->number + 1) / 2;
    if (p->type == P_SENSE) {
        float ratio = r->number <= .5f ? 2 + 4 * r->number : 4 + 12 * (r->number - .5f);
        scaled = (ratio - 2) / 998;
    }
    if (scaled < 0 || scaled > 1) return false;
    *value = (uint8_t)(scaled * 255 + .5f);
    return true;
}

bool controls_adopt(control_state_t *s, const probe_t *p) {
    if (p->phase != P_DONE || p->pending || p->count != PROBE_REPORTS || p->writes != 3) return false;
    bool found[CONTROL_MODULES] = {false}, caps[CONTROL_MODULES] = {false};
    unsigned amp = 0;
    for (unsigned i = 2; i < p->count; i++) {
        const uint8_t *r = p->reports[i];
        if (!memcmp(r, "\34\1\5\0", 4)) {
            if (amp) return false;
            amp = i;
        }
        if (r[0] == 0x21 && r[1] == 1 && r[2] >= 1 && r[2] <= 5) {
            unsigned m = r[2] == 5 ? 0 : r[2];
            if (caps[m] || r[3] > 29) return false;
            caps[m] = true;
            s->available_count[m] = r[3];
            for (unsigned j = 0; j < r[3]; j++)
                s->available[m][j] = r[6 + 2*j] | (uint16_t)r[7 + 2*j] << 8;
        }
    }
    if (!amp || amp + 6 >= p->count) return false;
    uint8_t slot = p->reports[amp][4];
    for (unsigned i = amp; i <= amp + 6; i++) {
        const uint8_t *r = p->reports[i];
        if (r[0] != 0x1c || r[1] != 1 || r[2] < 5 || r[2] > 9 || r[3]) continue;
        unsigned module = r[2] - 5;
        if (found[module] || r[4] >= 24 || r[6] != 1 || r[7] > 1) return false;
        if (!module) slot = r[4];
        found[module] = true;
        s->index[module] = i;
    }
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        if (!found[m] || !caps[m]) return false;
        const uint8_t *r = p->reports[s->index[m]];
        if (r[4] != slot || (!m && r[17]) || (m && r[18] > 7)) return false;
    }
    bool used[8] = {false};
    for (unsigned m = 1; m < CONTROL_MODULES; m++) {
        const uint8_t *r = p->reports[s->index[m]];
        if (!model_id(r)) continue;
        if (used[r[18]]) return false;
        used[r[18]] = true;
    }
    bool same_slot = s->valid && s->raw[0][4] == slot;
    if (!same_slot) s->gate = (control_gate_t){0};
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        memcpy(s->raw[m], p->reports[s->index[m]], 64);
        if (!same_slot || model_id(s->raw[m])) memcpy(s->latent[m], s->raw[m], 64);
    }
    s->valid = true;
    return true;
}

static void gate_sync(control_state_t *s, bool preset, unsigned *changed) {
    if (!s->gate.selected) return;
    if (preset || model_id(s->raw[1])) {
        s->gate = (control_gate_t){0};
        *changed |= 2;
        return;
    }
    bool bypassed = !s->raw[0][47];
    uint8_t threshold = bypassed ? s->gate.threshold : s->raw[0][48];
    if (bypassed != s->gate.bypassed || threshold != s->gate.threshold) *changed |= 2;
    s->gate.bypassed = bypassed;
    s->gate.threshold = threshold;
}

enum control_event controls_event(control_state_t *s, control_event_t *e,
                                 const uint8_t *p, size_t length, unsigned *changed) {
    *changed = 0;
    if (!s->valid || !p || length != 64) return E_BAD;
    if (p[0] == 5 && p[1] == 1 && p[2] == 2) {
        if (e->preset && !(e->mask & 1)) return E_BAD;
        if (e->mask && !(e->mask & 1)) {
            memcpy(e->raw[0], s->raw[0], 64);
            e->mask |= 1;
        }
        uint8_t *raw = e->mask & 1 ? e->raw[0] : s->raw[0];
        if (p[3] != raw[16] || p[4] != raw[17] || p[5] != p[6] ||
            p[5] >= 23 || p[11]) return E_BAD;
        unsigned offset = 32u + p[5];
        if (raw[offset] == p[10]) return E_NONE;
        raw[offset] = p[10];
        if (e->mask) return E_WAIT;
        memcpy(s->latent[0], raw, 64);
        for (unsigned m = 0; m < CONTROL_MODULES; m++) s->raw[m][7] = 1;
        *changed = 1;
        gate_sync(s, false, changed);
        return E_CHANGED;
    }
    if (p[0] != 0x1c || p[1] != 1) return E_NONE;
    if (p[3] || p[4] >= 24 || p[6] != 1 || p[7] > 1) return E_BAD;
    if (p[2] == 4) {
        if (e->mask) return E_BAD;
        e->slot = p[4]; e->preset = true;
        return E_WAIT;
    }
    if (p[2] >= 5 && p[2] <= 9) {
        unsigned m = p[2] - 5;
        if ((e->mask || e->preset) && e->slot != p[4]) return E_BAD;
        if ((!m && p[17]) || (m && p[18] > 7)) return E_BAD;
        e->slot = p[4];
        memcpy(e->raw[m], p, 64);
        e->mask |= 1u << m;
        return E_WAIT;
    }
    if (p[2] != 0 || !e->mask) return E_NONE;
    if (e->slot != p[4] || ((e->preset || e->slot != s->raw[0][4]) &&
        e->mask != (1u << CONTROL_MODULES) - 1)) return E_BAD;

    for (unsigned m = 0; m < CONTROL_MODULES; m++)
        if ((e->mask & (1u << m)) && e->raw[m][7] != p[7]) return E_BAD;
    bool used[8] = {false};
    for (unsigned m = 1; m < CONTROL_MODULES; m++) {
        const uint8_t *r = e->mask & (1u << m) ? e->raw[m] : s->raw[m];
        if (!model_id(r)) continue;
        if (used[r[18]]) return E_BAD;
        used[r[18]] = true;
    }
    bool new_slot = e->slot != s->raw[0][4];
    if (s->raw[0][7] != p[7]) *changed |= 1;
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        if (e->mask & (1u << m)) {
            if (new_slot || memcmp(s->raw[m] + 16, e->raw[m] + 16, 48))
                *changed |= 1u << m;
            memcpy(s->raw[m], e->raw[m], 64);
            memcpy(s->latent[m], s->raw[m], 64);
        }
        s->raw[m][4] = e->slot; s->raw[m][7] = p[7];
    }
    gate_sync(s, new_slot || e->preset, changed);
    *e = (control_event_t){0};
    return *changed || new_slot ? E_CHANGED : E_NONE;
}

const char *controls_model(const control_state_t *s, unsigned module) {
    if (!s->valid || module >= CONTROL_MODULES) return NULL;
    const uint8_t *raw = model_id(s->raw[module]) ? s->raw[module] : s->latent[module];
    const model_t *model = find_model(module, model_id(raw), NULL);
    if (module == 1 && s->gate.selected) return "DUBS_ChromeGate";
    return model ? model->name : "DUBS_Passthru";
}

static unsigned free_position(const control_state_t *s, unsigned module, unsigned preferred) {
    bool used[8] = {false};
    for (unsigned m = 1; m < CONTROL_MODULES; m++)
        if (m != module && model_id(s->raw[m])) used[s->raw[m][18]] = true;
    if (preferred < 8 && !used[preferred]) return preferred;
    for (unsigned i = 0; i < 8; i++) if (!used[i]) return i;
    return 8;
}

static unsigned empty_position(unsigned module) {
    return module < 3 ? module - 1 : module + 1;
}

bool controls_path(const control_state_t *s, const char *path[7]) {
    if (!s->valid) return false;
    unsigned order[CONTROL_MODULES] = {0, 1, 2, 3, 4}, weight[CONTROL_MODULES] = {8};
    for (unsigned m = 1; m < CONTROL_MODULES; m++) {
        const uint8_t *raw = model_id(s->raw[m]) ? s->raw[m] : s->latent[m];
        unsigned position = model_id(raw) ? raw[18] : empty_position(m);
        weight[m] = position * 2 + 1;
    }
    for (unsigned i = 1; i < CONTROL_MODULES; i++) {
        unsigned item = order[i], j = i;
        while (j && weight[order[j - 1]] > weight[item]) { order[j] = order[j - 1]; j--; }
        order[j] = item;
    }
    path[0] = path[6] = "preset";
    for (unsigned i = 0; i < CONTROL_MODULES; i++) path[i + 1] = nodes[order[i]];
    return true;
}

static void seed(uint8_t raw[64], const model_t *model) {
    memset(raw + 16, 0, 48);
    raw[16] = model->id; raw[17] = model->id >> 8;
    if (!model->module) {
        memcpy(raw + 32, model->defaults, 23);
        raw[40] = raw[43] = model->reserved;
        raw[44] = raw[45] = raw[46] = raw[50] = model->layout;
        raw[53] = 1;
    } else {
        memcpy(raw + 19, model->fixed, 3);
        memcpy(raw + 32, model->defaults, 6);
    }
}

static void packet(uint8_t out[64], const uint8_t raw[64]) {
    memcpy(out, raw, 64);
    memset(out, 0, 16);
    const uint8_t header[] = {0x1c, 3, raw[2], 0, 0, 0, 1, 1};
    memcpy(out, header, sizeof(header));
}

static void gate_prepare(const control_state_t *s, control_plan_t *plan) {
    plan->gate_update = true;
    plan->gate = s->gate;
    plan->amp_gate[0] = s->raw[0][41];
    plan->amp_gate[1] = s->raw[0][47];
    plan->amp_gate[2] = s->raw[0][48];
}

static bool gate_plan(const control_state_t *s, const control_request_t *r, control_plan_t *plan) {
    gate_prepare(s, plan);
    if (r->replace) {
        if (!s->gate.selected) memcpy(plan->gate.saved, plan->amp_gate, 3);
        plan->gate.selected = true;
        plan->gate.bypassed = false;
        plan->gate.threshold = 7;
        memset(plan->expected + 16, 0, 48);
        plan->expected[20] = 8; plan->expected[21] = 1;
        memcpy(plan->latent, plan->expected, 64);
        if (memcmp(s->raw[1] + 16, plan->expected + 16, 48))
            packet(plan->packets[plan->count++], plan->expected);
    } else if (!strcmp(r->parameter, "threshold")) {
        if (r->type != C_FLOAT || !isfinite(r->number) || r->number < -70 || r->number > -30) return false;
        float threshold = fminf(r->number, -42);
        plan->gate.threshold = (uint8_t)lroundf((threshold + 96) / 6);
    } else if (!strcmp(r->parameter, "bypass")) {
        if (r->type != C_BOOL || (r->number != 0 && r->number != 1)) return false;
        plan->gate.bypassed = r->number != 0;
    } else return false;
    plan->amp_gate[0] = 255;
    plan->amp_gate[1] = plan->gate.bypassed ? 0 : 5;
    plan->amp_gate[2] = plan->gate.bypassed ? 0 : plan->gate.threshold;
    return true;
}

bool controls_plan(const control_state_t *s, const control_request_t *r, control_plan_t *plan) {
    if (!s->valid) return false;
    unsigned module;
    for (module = 0; module < CONTROL_MODULES; module++) if (!strcmp(r->node, nodes[module])) break;
    if (module == CONTROL_MODULES) return false;
    *plan = (control_plan_t){.module = module};
    const uint8_t *old = s->raw[module];
    memcpy(plan->expected, old, 64);
    memcpy(plan->latent, s->latent[module], 64);
    uint8_t *next = plan->expected;
    const model_t *model = find_model(module, model_id(old), NULL);
    if (module == 1) {
        if ((r->replace && !strcmp(r->text, "DUBS_ChromeGate")) || (!r->replace && s->gate.selected))
            return gate_plan(s, r, plan);
        if (r->replace && s->gate.selected) {
            gate_prepare(s, plan);
            memcpy(plan->amp_gate, s->gate.saved, 3);
            plan->gate = (control_gate_t){0};
        }
    }
    if (r->replace) {
        if (module && !strcmp(r->text, "DUBS_Passthru")) {
            memset(next + 16, 0, 48);
            next[20] = 8; next[21] = 1;
            memcpy(plan->latent, next, 64);
        } else {
            model = find_model(module, 0, r->text);
            if (!model || (!module && old[54])) return false;
            bool supported = false;
            for (unsigned i = 0; i < s->available_count[module]; i++)
                if (s->available[module][i] == model->id) supported = true;
            if (!supported) return false;
            seed(next, model);
            if (!module) {
                next[41] = old[41]; next[47] = old[47]; next[48] = old[48];
            }
            memcpy(plan->latent, next, 64);
        }
    } else if (module && !strcmp(r->parameter, "bypass")) {
        if (r->type != C_BOOL || (r->number != 0 && r->number != 1)) return false;
        if (r->number) {
            if (!model_id(old)) return true;
            memcpy(plan->latent, old, 64);
            memset(next + 16, 0, 48);
            next[20] = 8; next[21] = 1;
        } else {
            if (model_id(old)) return true;
            if (!model_id(plan->latent)) return false;
            memcpy(next + 16, plan->latent + 16, 48);
        }
    } else {
        bool bypassed = module && !model_id(old);
        if (bypassed) {
            model = find_model(module, model_id(plan->latent), NULL);
            if (!model) return false;
        }
        if (!model) return false;
        if (module && !strcmp(r->parameter, "bypassType")) {
            if (r->type != C_STRING || (strcmp(r->text, "Pre") && strcmp(r->text, "Post"))) return false;
            unsigned first = !strcmp(r->text, "Pre") ? 0 : 4, pos = 8;
            const uint8_t *raw = bypassed ? plan->latent : next;
            if (raw[18] >= first && raw[18] < first + 4) return true;
            for (unsigned i = first; i < first + 4; i++)
                if (free_position(s, module, i) == i) { pos = i; break; }
            if (pos > 7) return false;
            if (bypassed) { plan->latent[18] = pos; return true; }
            next[18] = pos;
            memcpy(plan->latent, next, 64);
            packet(plan->packets[plan->count++], next);
            return true;
        }
        const param_t *p = NULL;
        for (size_t i = 0; i < model->count; i++)
            if (!strcmp(model->params[i].name, r->parameter)) p = &model->params[i];
        if (!p) return false;
        uint8_t value;
        if (!encode(p, r, &value)) return false;
        if (!module && p->offset == 47 && value != old[47]) return false;
        uint8_t *destination = bypassed ? plan->latent : next;
        if (p->type == P_BLEND && destination[p->offset] < 128) value = 255 - value;
        destination[p->offset] = value;
        if (bypassed) return true;
        memcpy(plan->latent, next, 64);
    }
    if (module) {
        bool installing = model_id(next) && !model_id(old) && !model_id(s->latent[module]);
        unsigned preferred = model_id(old) ? old[18] :
            installing ? empty_position(module) : s->latent[module][18];
        unsigned pos = free_position(s, module, preferred);
        if (pos > 7 || (installing && pos != preferred)) return false;
        next[18] = pos;
        if (model_id(next)) memcpy(plan->latent, next, 64);
    }
    if (!memcmp(old + 16, next + 16, 48)) return true;
    if (module && model_id(old) && model_id(next) && model_id(old) != model_id(next)) {
        uint8_t clear[64]; memcpy(clear, old, 64);
        memset(clear + 16, 0, 48); clear[18] = next[18]; clear[20] = 8; clear[21] = 1;
        packet(plan->packets[plan->count++], clear);
    }
    packet(plan->packets[plan->count++], next);
    return true;
}

void controls_accept(control_state_t *s, const control_plan_t *p) {
    bool changed = memcmp(s->raw[p->module] + 16, p->expected + 16, 48) != 0 ||
                   memcmp(s->latent[p->module] + 16, p->latent + 16, 48) != 0;
    if (p->gate_update) {
        static const unsigned offsets[] = {41, 47, 48};
        changed |= memcmp(&s->gate, &p->gate, sizeof(s->gate)) != 0;
        for (unsigned i = 0; i < 3; i++) {
            changed |= s->raw[0][offsets[i]] != p->amp_gate[i];
            s->raw[0][offsets[i]] = s->latent[0][offsets[i]] = p->amp_gate[i];
        }
    }
    memcpy(s->raw[p->module], p->expected, 64);
    controls_commit(s, p);
    if (changed) for (unsigned m = 0; m < CONTROL_MODULES; m++) s->raw[m][7] = 1;
}

bool controls_diff(const control_state_t *wanted, const control_state_t *actual,
                   unsigned module, control_plan_t *plan) {
    *plan = (control_plan_t){.module = module};
    const uint8_t *next = wanted->raw[module], *old = actual->raw[module];
    memcpy(plan->expected, next, 64);
    memcpy(plan->latent, wanted->latent[module], 64);
    if (!memcmp(old + 16, next + 16, 48)) return false;
    if (module && model_id(old) && model_id(next) && model_id(old) != model_id(next)) {
        uint8_t clear[64]; memcpy(clear, old, 64);
        memset(clear + 16, 0, 48);
        clear[18] = next[18]; clear[20] = 8; clear[21] = 1;
        packet(plan->packets[plan->count++], clear);
    }
    packet(plan->packets[plan->count++], next);
    return true;
}

void controls_commit(control_state_t *s, const control_plan_t *p) {
    memcpy(s->latent[p->module], p->latent, 64);
    if (p->gate_update) s->gate = p->gate;
}

size_t controls_values(const control_state_t *s, const char *node,
                       control_request_t values[CONTROL_VALUES]) {
    if (!s->valid) return 0;
    unsigned m;
    for (m = 0; m < CONTROL_MODULES; m++) if (!strcmp(nodes[m], node)) break;
    if (m == CONTROL_MODULES) return 0;
    if (m == 1 && s->gate.selected) {
        values[0] = (control_request_t){.node = "stomp", .parameter = "threshold",
            .type = C_FLOAT, .number = -96 + 6 * s->gate.threshold};
        return 1;
    }
    bool bypass = m && !model_id(s->raw[m]);
    const uint8_t *raw = bypass ? s->latent[m] : s->raw[m];
    const model_t *model = find_model(m, model_id(raw), NULL);
    if (!model || model->count > CONTROL_VALUES) return 0;
    size_t count = 0;
    for (size_t i = 0; i < model->count; i++) {
        if (!(model->ui_mask & (1u << i))) continue;
        values[count] = (control_request_t){0};
        const param_t *p = &model->params[i];
        if (!decode(p, raw[p->offset], &values[count++])) return 0;
    }
    for (size_t i = 0; i < count; i++) snprintf(values[i].node, sizeof(values[i].node), "%s", node);
    return count;
}

typedef struct { char *p; size_t left, used; bool failed; } output_t;
static void append(output_t *o, const char *format, ...) {
    if (o->failed) return;
    va_list args; va_start(args, format);
    int n = vsnprintf(o->p, o->left, format, args);
    va_end(args);
    if (n < 0 || (size_t)n >= o->left) { o->failed = true; return; }
    o->p += n; o->left -= n; o->used += n;
}

size_t controls_json(const control_state_t *s, char *json, size_t size) {
    if (!s->valid) return 0;
    output_t out = {.p = json, .left = size};
    append(&out, "[");
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        if (m == 1 && s->gate.selected) {
            append(&out, ",{\"nodeId\":\"stomp\",\"nodeType\":\"dspUnit\",\"FenderId\":\"DUBS_ChromeGate\","
                "\"dspUnitParameters\":{\"bypass\":%s,\"threshold\":%d,\"gateDetectorPosition\":\"jack\"}}",
                s->gate.bypassed ? "true" : "false", -96 + 6 * s->gate.threshold);
            continue;
        }
        bool bypass = m && !model_id(s->raw[m]);
        const uint8_t *raw = bypass ? s->latent[m] : s->raw[m];
        const model_t *model = find_model(m, model_id(raw), NULL);
        if (!model && !m) return 0;
        const char *name = model ? model->name : "DUBS_Passthru";
        append(&out, "%s{\"nodeId\":\"%s\",\"nodeType\":\"dspUnit\",\"FenderId\":\"%s\",",
               m ? "," : "", nodes[m], name);
        if (!model && model_id(raw)) append(&out, "\"torinoNativeModel\":%u,", model_id(raw));
        append(&out, "\"dspUnitParameters\":{");
        bool comma = false;
        if (m && model) {
            append(&out, "\"bypass\":%s,\"bypassType\":\"%s\"", bypass ? "true" : "false", raw[18] < 4 ? "Pre" : "Post");
            comma = true;
        }
        if (model) for (size_t i = 0; i < model->count; i++) {
            const param_t *p = &model->params[i];
            control_request_t value = {0};
            if (!decode(p, raw[p->offset], &value)) return 0;
            append(&out, "%s\"%s\":", comma ? "," : "", p->name);
            comma = true;
            if (value.type == C_STRING) append(&out, "\"%s\"", value.text);
            else if (value.type == C_BOOL) append(&out, "%s", value.number ? "true" : "false");
            else append(&out, "%.9g", (double)value.number);
        }
        append(&out, "}}");
    }
    append(&out, "]");
    return out.failed ? 0 : out.used;
}
