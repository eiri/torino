#include "preset.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned start, end, next; char type; } token_t;
static token_t tokens[512];
static unsigned count;
static const char *source;
static size_t length, cursor;

static void space(void) {
    while (cursor < length && strchr(" \t\r\n", source[cursor])) cursor++;
}

static bool number(const char *p, size_t n, float *out) {
    if (!n || n >= 48) return false;
    size_t i = p[0] == '-' ? 1 : 0;
    if (i == n) return false;
    if (p[i] == '0') i++;
    else {
        if (p[i] < '1' || p[i] > '9') return false;
        while (i < n && p[i] >= '0' && p[i] <= '9') i++;
    }
    if (i < n && p[i] == '.') {
        size_t first = ++i;
        while (i < n && p[i] >= '0' && p[i] <= '9') i++;
        if (i == first) return false;
    }
    if (i < n && (p[i] == 'e' || p[i] == 'E')) {
        i++;
        if (i < n && (p[i] == '+' || p[i] == '-')) i++;
        size_t first = i;
        while (i < n && p[i] >= '0' && p[i] <= '9') i++;
        if (i == first) return false;
    }
    if (i != n) return false;
    char text[48]; memcpy(text, p, n); text[n] = 0;
    *out = strtof(text, NULL);
    return isfinite(*out);
}

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool scan(unsigned depth) {
    space();
    if (depth > 8 || count == 512 || cursor == length) return false;
    unsigned index = count++;
    token_t *t = &tokens[index];
    t->start = cursor;
    t->type = source[cursor++];
    if (t->type == '{' || t->type == '[') {
        char end = t->type == '{' ? '}' : ']';
        space();
        if (cursor < length && source[cursor] == end) cursor++;
        else for (;;) {
            if (t->type == '{') {
                space();
                if (cursor == length || source[cursor] != '"' || !scan(depth + 1)) return false;
                space();
                if (cursor == length || source[cursor++] != ':') return false;
            }
            if (!scan(depth + 1)) return false;
            space();
            if (cursor == length) return false;
            char c = source[cursor++];
            if (c == end) break;
            if (c != ',') return false;
        }
    } else if (t->type == '"') {
        bool closed = false;
        while (cursor < length) {
            unsigned char c = (unsigned char)source[cursor++];
            if (c == '"') { closed = true; break; }
            if (c < 32) return false;
            if (c != '\\') continue;
            if (cursor == length) return false;
            c = (unsigned char)source[cursor++];
            if (c == 'u') {
                for (unsigned i = 0; i < 4; i++)
                    if (cursor == length || hex(source[cursor++]) < 0) return false;
            } else if (!strchr("\"\\/bfnrt", c)) return false;
        }
        if (!closed) return false;
    } else {
        while (cursor < length && !strchr(" \t\r\n,}]", source[cursor])) cursor++;
        size_t n = cursor - t->start;
        const char *p = source + t->start;
        float value;
        if (!((n == 4 && (!memcmp(p, "true", 4) || !memcmp(p, "null", 4))) ||
              (n == 5 && !memcmp(p, "false", 5)) || number(p, n, &value))) return false;
    }
    t->end = cursor; t->next = count;
    return true;
}

static bool text(int index, char *out, size_t size) {
    if (index < 0 || tokens[index].type != '"') return false;
    const token_t *t = &tokens[index];
    size_t n = 0;
    for (unsigned i = t->start + 1; i < t->end - 1; i++) {
        unsigned char c = (unsigned char)source[i];
        if (c == '\\') {
            c = (unsigned char)source[++i];
            static const char escapes[] = "\"\\/bfnrt";
            const char *esc = strchr(escapes, c);
            if (c == 'u') {
                unsigned value = 0;
                for (unsigned j = 0; j < 4; j++) value = 16 * value + (unsigned)hex(source[++i]);
                if (!value || value > 127) return false;
                c = value;
            } else if (esc) c = (unsigned char)"\"\\/\b\f\n\r\t"[esc - escapes];
            else return false;
        }
        if (!c || n + 1 >= size) return false;
        out[n++] = c;
    }
    out[n] = 0;
    return true;
}

static int member(int object, const char *key) {
    if (object < 0 || tokens[object].type != '{') return -1;
    int found = -1;
    for (unsigned i = object + 1; i < tokens[object].next; i = tokens[i + 1].next) {
        char name[48];
        if (!text(i, name, sizeof(name))) return -1;
        if (!strcmp(name, key)) {
            if (found >= 0) return -1;
            found = i + 1;
        }
    }
    return found;
}

static bool apply(control_state_t *s, control_request_t *r) {
    if (!r->replace && !strcmp(r->parameter, "bypassType"))
        return r->type == C_STRING && (!strcmp(r->text, "Pre") || !strcmp(r->text, "Post"));
    if (!r->replace && !strcmp(r->parameter, "gateDetectorPosition"))
        return r->type == C_STRING && !strcmp(r->text, "jack");
    if (!r->replace && !strcmp(r->node, "amp") && !strcmp(r->parameter, "gatePreset")) {
        static const char *const modes[] = {"off", "low", "mid", "high", "super"};
        if (r->type != C_STRING) return false;
        unsigned mode;
        for (mode = 0; mode < 5; mode++) if (!strcmp(r->text, modes[mode])) break;
        if (mode == 5) return false;

        s->raw[0][41] = s->latent[0][41] = mode == 1 ? 0 : 255;
        s->raw[0][47] = s->latent[0][47] = mode;
        s->raw[0][48] = s->latent[0][48] = mode ? mode + 1 : 0;
        return true;
    }
    control_plan_t plan;
    if (!controls_plan(s, r, &plan)) return false;
    controls_accept(s, &plan);
    return true;
}

static bool parameter(int index, control_request_t *r) {
    token_t *t = &tokens[index];
    if (t->type == '"') { r->type = C_STRING; return text(index, r->text, sizeof(r->text)); }
    if (t->type == 't' || t->type == 'f') {
        r->type = C_BOOL; r->number = t->type == 't'; return true;
    }
    r->type = C_FLOAT;
    return number(source + t->start, t->end - t->start, &r->number);
}

static int node_index(int object) {
    char id[16];
    if (!text(member(object, "nodeId"), id, sizeof(id))) return -1;
    if (!strcmp(id, "preset")) return CONTROL_MODULES;
    for (unsigned m = 0; m < CONTROL_MODULES; m++) if (!strcmp(id, controls_node(m))) return m;
    return -1;
}

static bool routing(int graph, control_state_t *s) {
    int list = member(graph, "connections"), next[2][6];
    if (list < 0 || tokens[list].type != '[') return false;
    for (unsigned ch = 0; ch < 2; ch++) for (unsigned n = 0; n < 6; n++) next[ch][n] = -1;
    unsigned edges = 0;
    for (unsigned i = list + 1; i < tokens[list].next; i = tokens[i].next) {
        int input = member(i, "input"), output = member(i, "output");
        int from = node_index(input), to = node_index(output);
        int a = member(input, "index"), b = member(output, "index");
        control_request_t x = {0}, y = {0};
        if (from < 0 || to < 0 || a < 0 || b < 0 || !parameter(a, &x) || !parameter(b, &y) ||
            x.type != C_FLOAT || y.type != C_FLOAT || x.number != y.number ||
            (x.number != 0 && x.number != 1)) return false;
        unsigned ch = (unsigned)x.number;
        if (next[ch][from] >= 0) return false;
        next[ch][from] = to; edges++;
    }
    if (edges != 12) return false;
    bool used[5] = {false}, after_amp = false;
    unsigned pre = 0, post = 4;
    int from = CONTROL_MODULES;
    for (unsigned i = 0; i < CONTROL_MODULES; i++) {
        int to = next[0][from];
        if (to < 0 || to >= CONTROL_MODULES || next[1][from] != to || used[to]) return false;
        used[to] = true;
        if (!to) after_amp = true;
        else {
            unsigned pos = after_amp ? post++ : pre++;
            if (pos > 7) return false;
            s->raw[to][18] = s->latent[to][18] = pos;
        }
        from = to;
    }
    return next[0][from] == CONTROL_MODULES && next[1][from] == CONTROL_MODULES;
}

bool preset_decode(const char *json, size_t size, const control_state_t *base,
                   control_state_t *draft, char name[33]) {
    if (!json || !size || size >= 6144 || !base->valid || memchr(json, 0, size)) return false;
    source = json; length = size; cursor = count = 0;
    if (!scan(0)) return false;
    space();
    if (cursor != length || tokens[0].type != '{') return false;
    int info = member(0, "info"), graph = member(0, "audioGraph");
    char type[32];
    if (!text(member(0, "nodeType"), type, sizeof(type)) || strcmp(type, "preset") ||
        !text(member(info, "product_id"), type, sizeof(type)) || strcmp(type, "mustang-lt") ||
        !text(member(info, "displayName"), name, 33)) return false;
    int list = member(graph, "nodes");
    if (list < 0 || tokens[list].type != '[') return false;
    int nodes[CONTROL_MODULES] = {-1, -1, -1, -1, -1};
    for (unsigned i = list + 1; i < tokens[list].next; i = tokens[i].next) {
        char id[16];
        if (!text(member(i, "nodeType"), type, sizeof(type)) || strcmp(type, "dspUnit") ||
            !text(member(i, "nodeId"), id, sizeof(id))) return false;
        unsigned m;
        for (m = 0; m < CONTROL_MODULES; m++) if (!strcmp(id, controls_node(m))) break;
        if (m == CONTROL_MODULES || nodes[m] >= 0) return false;
        nodes[m] = i;
    }

    *draft = *base;
    if (draft->gate.selected) {
        draft->raw[0][41] = draft->latent[0][41] = draft->gate.saved[0];
        draft->raw[0][47] = draft->latent[0][47] = draft->gate.saved[1];
        draft->raw[0][48] = draft->latent[0][48] = draft->gate.saved[2];
    }
    draft->gate = (control_gate_t){0};
    draft->raw[0][41] = draft->latent[0][41] = 255;
    draft->raw[0][47] = draft->latent[0][47] = 0;
    draft->raw[0][48] = draft->latent[0][48] = 0;
    for (unsigned m = 1; m < CONTROL_MODULES; m++) {
        memset(draft->raw[m] + 16, 0, 48);
        memset(draft->latent[m] + 16, 0, 48);
    }
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        if (nodes[m] < 0) return false;
        control_request_t r = {.replace = true};
        strcpy(r.node, controls_node(m));
        int model = member(nodes[m], "FenderId");
        if (model < 0 || tokens[model].type != '"') return false;
        if (!text(model, r.text, sizeof(r.text)) || !apply(draft, &r)) {
            strcpy(r.text, m ? "DUBS_Passthru" : "DUBS_LinearGain");
            if (!apply(draft, &r)) return false;
        }
        bool passthru = !strcmp(r.text, "DUBS_Passthru");
        int params = member(nodes[m], "dspUnitParameters");
        if (params < 0 || tokens[params].type != '{') return false;
        int bypass = member(params, "bypass");
        for (unsigned i = params + 1; i < tokens[params].next; i = tokens[i + 1].next) {
            r = (control_request_t){0}; strcpy(r.node, controls_node(m));
            if (!text(i, r.parameter, sizeof(r.parameter)) || member(params, r.parameter) != (int)i + 1) return false;
            if (!strcmp(r.parameter, "bypass")) continue;
            if (passthru || !parameter(i + 1, &r)) continue;

            if (!strcmp(r.parameter, "noteDivision") || !strcmp(r.parameter, "tapTimeBPM") ||
                !strcmp(r.parameter, "stereoSpread")) continue;

            (void)apply(draft, &r);
        }
        if (bypass >= 0) {
            r = (control_request_t){0}; strcpy(r.node, controls_node(m));
            strcpy(r.parameter, "bypass");
            if (!passthru && parameter(bypass, &r) && r.type == C_BOOL)
                (void)apply(draft, &r);
        }
    }
    if (!routing(graph, draft)) return false;
    for (unsigned m = 0; m < CONTROL_MODULES; m++) draft->raw[m][7] = 1;
    return true;
}
