#include "controls.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static probe_t capture(void) {
    probe_t p = {.phase = P_DONE, .count = PROBE_REPORTS, .writes = 3};
    const uint16_t ids[] = {117, 186, 19, 22, 38};
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        uint8_t *r = p.reports[51 + m];
        r[0] = 0x1c; r[1] = 1; r[2] = 5 + m; r[4] = 13; r[6] = 1;
        r[16] = ids[m]; r[17] = ids[m] >> 8; r[18] = m < 3 ? m - !!m : m + 1;
        for (unsigned j = 32; j < 64; j++) r[j] = (uint8_t)(j * 3);
        uint8_t *c = p.reports[140 + m];
        c[0] = 0x21; c[1] = 1; c[2] = m ? m : 5; c[3] = 1;
        c[6] = ids[m]; c[7] = ids[m] >> 8;
    }
    uint8_t *a = p.reports[51];
    a[33] = 51; a[47] = 0; a[49] = 9; a[51] = 1; a[52] = 1; a[54] = 0;
    return p;
}

static control_request_t request(const char *node, const char *parameter, float value) {
    control_request_t r = {.type = C_FLOAT, .number = value};
    snprintf(r.node, sizeof(r.node), "%s", node);
    snprintf(r.parameter, sizeof(r.parameter), "%s", parameter);
    return r;
}

static void adopt_plan(control_state_t *s, probe_t *p, const control_plan_t *plan) {
    memcpy(p->reports[s->index[plan->module]] + 16, plan->expected + 16, 48);
    assert(controls_adopt(s, p));
    controls_commit(s, plan);
}

static void parameters(void) {
    probe_t p = capture();
    control_state_t s = {0};
    control_plan_t plan;
    assert(controls_adopt(&s, &p));
    const char *names[] = {"gain", "treb", "mid", "bass", "bias"};
    const unsigned offsets[] = {33, 36, 37, 38, 42};
    for (unsigned i = 0; i < 5; i++) {
        control_request_t r = request("amp", names[i], .75f);
        assert(controls_plan(&s, &r, &plan) && plan.count == 1 && plan.module == 0);
        assert(plan.expected[offsets[i]] == 191);
        for (unsigned j = 16; j < 64; j++)
            assert(plan.expected[j] == (j == offsets[i] ? 191 : s.raw[0][j]));
        for (unsigned j = 0; j < 16; j++) {
            static const uint8_t header[16] = {0x1c, 3, 5, 0, 0, 0, 1, 1};
            assert(plan.packets[0][j] == header[j]);
        }
    }
    control_request_t r = request("amp", "gain", NAN);
    assert(!controls_plan(&s, &r, &plan));
    r.number = 1.01f;
    assert(!controls_plan(&s, &r, &plan));
    r = request("amp", "presence", .5f);
    assert(!controls_plan(&s, &r, &plan));
    r = request("amp", "bright", 0); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan) && plan.expected[52] == 0);
    r.type = C_FLOAT;
    assert(!controls_plan(&s, &r, &plan));
    r = request("amp", "gatePreset", 0); r.type = C_STRING;
    strcpy(r.text, "high");
    assert(!controls_plan(&s, &r, &plan) && !plan.count);
    strcpy(r.text, "off");
    assert(controls_plan(&s, &r, &plan) && !plan.count);
    strcpy(r.text, "unknown");
    assert(!controls_plan(&s, &r, &plan));
    r = request("mod", "rateHz", 10);
    assert(controls_plan(&s, &r, &plan) && plan.expected[33] == 255);
    r.number = .08f;
    assert(controls_plan(&s, &r, &plan) && plan.expected[33] == 0);
    r = request("delay", "time", 1.0f);
    assert(controls_plan(&s, &r, &plan) && plan.expected[33] == 229);
    r = request("reverb", "tone", .25f);
    assert(controls_plan(&s, &r, &plan) && plan.expected[36] == 64);
}

static void round_trips(void) {
    probe_t p = capture(); control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    for (unsigned value = 0; value < 256; value++) {
        s.raw[0][32] = value; s.raw[1][35] = value;
        char json[4096]; assert(controls_json(&s, json, sizeof(json)));
        const char *volume = strstr(json, "\"volume\":"); assert(volume);
        control_request_t r = request("amp", "volume", strtof(volume + 9, NULL));
        assert(controls_plan(&s, &r, &plan) && !plan.count);
        const char *blend = strstr(json, "\"blend\":"); assert(blend);
        r = request("stomp", "blend", strtof(blend + 8, NULL));
        assert(controls_plan(&s, &r, &plan) && !plan.count);
    }
}

static void bypass(void) {
    probe_t p = capture();
    control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    control_request_t r = request("stomp", "bypass", 1); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan) && plan.count == 1);
    assert(!plan.expected[16] && !plan.expected[17]);
    uint8_t original[64]; memcpy(original, s.raw[1], 64);
    adopt_plan(&s, &p, &plan);
    char json[4096];
    assert(controls_json(&s, json, sizeof(json)) && strstr(json, "\"bypass\":true"));
    assert(strstr(json, "DUBS_Greenbox"));
    control_request_t values[CONTROL_VALUES];
    size_t count = controls_values(&s, "stomp", values);
    assert(count == 4);
    for (size_t i = 0; i < count; i++) {
        assert(strcmp(values[i].parameter, "bypass"));
        assert(strcmp(values[i].parameter, "bypassType"));
    }
    count = controls_values(&s, "reverb", values);
    assert(count == 3);
    assert(!strcmp(values[0].parameter, "level"));
    assert(!strcmp(values[1].parameter, "decay"));
    assert(!strcmp(values[2].parameter, "tone"));
    r = request("stomp", "gain", .25f);
    assert(controls_plan(&s, &r, &plan) && !plan.count);
    controls_commit(&s, &plan);
    assert(!s.raw[1][16] && s.latent[1][33] == 64);
    r = request("stomp", "bypass", 0); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan) && plan.count == 1);
    assert(plan.expected[16] == original[16] && plan.expected[33] == 64);
    for (unsigned j = 16; j < 64; j++) if (j != 33) assert(plan.expected[j] == original[j]);
    adopt_plan(&s, &p, &plan);
    assert(s.raw[1][16] == 186 && s.raw[1][33] == 64);
}

static void models(void) {
    probe_t p = capture();
    control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    control_request_t r = {.node = "amp", .text = "DUBS_Jcm800", .replace = true};
    assert(!controls_plan(&s, &r, &plan));
    s.available[0][s.available_count[0]++] = 94;
    assert(controls_plan(&s, &r, &plan) && plan.count == 1);
    assert(plan.expected[16] == 94 && plan.expected[44] == 9 && plan.expected[40] == 128);
    assert(plan.expected[54] == 0 && plan.expected[41] == s.raw[0][41]);
    assert(plan.expected[47] == s.raw[0][47] && plan.expected[48] == s.raw[0][48]);
    adopt_plan(&s, &p, &plan);
    char json[4096]; assert(controls_json(&s, json, sizeof(json)));
    assert(strstr(json, "DUBS_Jcm800") && strstr(json, "\"master\":"));
    r = (control_request_t){.node = "stomp", .text = "DUBS_Blackbox", .replace = true};
    s.available[1][s.available_count[1]++] = 273;
    assert(controls_plan(&s, &r, &plan) && plan.count == 2);
    assert(!plan.packets[0][16] && plan.packets[1][16] == 17 && plan.packets[1][17] == 1);
    assert(plan.expected[18] == 0);
    strcpy(r.text, "DUBS_MythicDrive");
    assert(!controls_plan(&s, &r, &plan));
    r = (control_request_t){.node = "reverb", .parameter = "bypassType", .type = C_STRING, .text = "Pre"};
    assert(controls_plan(&s, &r, &plan) && plan.expected[18] == 2);
    adopt_plan(&s, &p, &plan);
    const char *path[7]; assert(controls_path(&s, path));
    assert(!strcmp(path[3], "reverb") && !strcmp(path[4], "amp"));
}

static void bounds(void) {
    probe_t p = capture();
    control_state_t s = {0};
    assert(controls_adopt(&s, &p));
    char json[4096]; assert(controls_json(&s, json, sizeof(json)));
    assert(strstr(json, "\"gain\":0.2") && strstr(json, "\"bass\":"));
    for (size_t size = 0; size < strlen(json) + 1; size++) {
        char small[4096];
        assert(!controls_json(&s, small, size));
    }
    // Factory effect reports are not current modules; duplicate current positions are invalid.
    memcpy(p.reports[80], p.reports[52], 64); p.reports[80][6] = 2;
    assert(controls_adopt(&s, &p));
    p.reports[53][18] = p.reports[52][18];
    assert(!controls_adopt(&s, &p));
    p = capture(); p.reports[141][3] = 30;
    assert(!controls_adopt(&s, &p));
}

static void catalog(void) {
    static const struct { const char *name; unsigned module; uint16_t id; } entries[] = {
        {"LinearGain",0,241}, {"Champ57",0,124}, {"Deluxe57",0,103}, {"Twin57",0,246},
        {"Bassman59",0,100}, {"Princeton65",0,106}, {"Deluxe65",0,83}, {"Twin65",0,117},
        {"Silvertone",0,249}, {"DR103",0,255}, {"Ac30Tb",0,97}, {"Plexi87",0,121},
        {"Jcm800",0,94}, {"Or120",0,252}, {"SuperSonic",0,114}, {"Rect2",0,93}, {"Evh3",0,109},
        {"Overdrive",1,60}, {"Greenbox",1,186}, {"Blackbox",1,273}, {"BigFuzz",1,271}, {"VariFuzz",1,26}, {"SimpleCompressor",1,136}, {"Sustain",1,7},
        {"ChorusTriangle",2,19}, {"TriangleFlanger",2,25}, {"Vibratone",2,45}, {"SineTremolo",2,65},
        {"Phaser",2,79}, {"StepFilter",2,41}, {"EcFilter",2,245},
        {"MonoDelay",3,22}, {"TapeDelayLite",3,43}, {"ReverseDelay",3,70},
        {"LargeHallReverb",4,58}, {"SmallRoomReverb",4,38}, {"Spring65",4,11}, {"LargePlate",4,75}, {"ArenaReverb",4,77},
    };
    probe_t p = capture(); control_state_t s = {0};
    assert(controls_adopt(&s, &p));
    memset(s.available_count, 0, sizeof(s.available_count));
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        unsigned m = entries[i].module;
        s.available[m][s.available_count[m]++] = entries[i].id;
    }
    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        unsigned m = entries[i].module;
        control_request_t r = {.replace = true}; control_plan_t plan;
        snprintf(r.node, sizeof(r.node), "%s", controls_node(m));
        snprintf(r.text, sizeof(r.text), "DUBS_%s", entries[i].name);
        assert(controls_plan(&s, &r, &plan));
        assert(plan.expected[16] == (entries[i].id & 255) && plan.expected[17] == entries[i].id >> 8);
        if (!m) {
            assert(plan.expected[41] == s.raw[0][41]);
            assert(plan.expected[47] == s.raw[0][47] && plan.expected[48] == s.raw[0][48]);
        }
        control_state_t trial = s;
        memcpy(trial.raw[m] + 16, plan.expected + 16, 48);
        controls_commit(&trial, &plan);
        char json[4096]; assert(controls_json(&trial, json, sizeof(json)) && strstr(json, r.text));
        control_request_t values[CONTROL_VALUES];
        size_t count = controls_values(&trial, r.node, values);
        assert(count && count <= CONTROL_VALUES);
        for (size_t j = 0; j < count; j++) assert(!strcmp(values[j].node, r.node));
    }
}

static void vari_fuzz(void) {
    probe_t p = capture(); control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    control_request_t r = {.node = "stomp", .text = "DUBS_VariFuzz", .replace = true};
    assert(!controls_plan(&s, &r, &plan));
    s.available[1][s.available_count[1]++] = 26;
    assert(controls_plan(&s, &r, &plan) && plan.count == 2);
    assert(!plan.packets[0][16] && plan.packets[1][16] == 26);
    assert(plan.expected[19] == 0 && plan.expected[20] == 8 && plan.expected[21] == 1);
    assert(plan.expected[34] == 0 && plan.expected[35] == 128 && plan.expected[36] == 128);
    controls_accept(&s, &plan);
    assert(!strcmp(controls_model(&s, 1), "DUBS_VariFuzz"));

    // Shared controls keep every native byte and never alter the hidden knobs.
    const char *shared[] = {"level", "gain"};
    for (unsigned i = 0; i < 2; i++) for (unsigned v = 0; v < 256; v++) {
        r = request("stomp", shared[i], (float)v / 255);
        assert(controls_plan(&s, &r, &plan) && plan.expected[32 + i] == v);
        for (unsigned j = 16; j < 64; j++)
            if (j != 32 + i) assert(plan.expected[j] == s.raw[1][j]);
        controls_accept(&s, &plan);
        control_request_t values[CONTROL_VALUES];
        assert(controls_values(&s, "stomp", values) == 3);
        assert(!strcmp(values[i].parameter, shared[i]));
        assert(fabsf(values[i].number - (float)v / 255) < .000001f);
    }

    // Vari edits change only Low, and both UI read paths return the selected mode.
    const char *modes[] = {"tight", "normal", "loose"};
    const uint8_t low[] = {0, 29, 128};
    for (unsigned i = 0; i < 3; i++) {
        r = (control_request_t){.node = "stomp", .parameter = "tone", .type = C_STRING};
        strcpy(r.text, modes[i]);
        assert(controls_plan(&s, &r, &plan) && plan.count == 1 && plan.expected[35] == low[i]);
        for (unsigned j = 16; j < 64; j++)
            if (j != 35) assert(plan.expected[j] == s.raw[1][j]);
        controls_accept(&s, &plan);
        assert(controls_plan(&s, &r, &plan) && !plan.count);
        control_request_t values[CONTROL_VALUES];
        assert(controls_values(&s, "stomp", values) == 3);
        assert(values[2].type == C_STRING && !strcmp(values[2].text, modes[i]));
        char json[4096], expected[48];
        snprintf(expected, sizeof(expected), "\"tone\":\"%s\"", modes[i]);
        assert(controls_json(&s, json, sizeof(json)) && strstr(json, expected));
    }
    strcpy(r.text, "unknown");
    assert(!controls_plan(&s, &r, &plan));
    r.type = C_FLOAT;
    assert(!controls_plan(&s, &r, &plan));
    r = request("stomp", "octave", .5f);
    assert(!controls_plan(&s, &r, &plan));

    // Physical Low values select the nearest mode without rewriting native bytes.
    for (unsigned v = 0; v < 256; v++) {
        s.raw[1][35] = v;
        control_request_t values[CONTROL_VALUES];
        assert(controls_values(&s, "stomp", values) == 3);
        assert(!strcmp(values[2].text, modes[v <= 14 ? 0 : v <= 78 ? 1 : 2]));
        assert(s.raw[1][35] == v);
    }

    // Bypassed edits update the retained effect, then return in the enable packet.
    r = request("stomp", "bypass", 1); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    r = (control_request_t){.node = "stomp", .parameter = "tone", .type = C_STRING, .text = "normal"};
    assert(controls_plan(&s, &r, &plan) && !plan.count); controls_accept(&s, &plan);
    assert(!s.raw[1][16] && s.latent[1][35] == 29);
    r = request("stomp", "bypass", 0); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan) && plan.count == 1);
    assert(plan.expected[16] == 26 && plan.expected[35] == 29);
}

static void sustain(void) {
    probe_t p = capture(); control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    control_request_t r = {.node = "stomp", .text = "DUBS_Sustain", .replace = true};
    assert(!controls_plan(&s, &r, &plan));
    s.available[1][s.available_count[1]++] = 7;
    assert(controls_plan(&s, &r, &plan) && plan.count == 2);
    assert(!plan.packets[0][16] && plan.packets[1][16] == 7);
    assert(plan.expected[33] == 64 && plan.expected[35] == 13 && plan.expected[36] == 128);
    controls_accept(&s, &plan);
    assert(!strcmp(controls_model(&s, 1), "DUBS_Sustain"));

    // Sense is monotonic, bounded, and round-trips through native ratio quantization.
    unsigned previous = 0;
    for (unsigned i = 0; i <= 100; i++) {
        r = request("stomp", "sensitivity", (float)i / 100);
        assert(controls_plan(&s, &r, &plan));
        assert(plan.expected[34] >= previous && plan.expected[34] <= 2);
        previous = plan.expected[34];
        for (unsigned j = 16; j < 64; j++)
            if (j != 34) assert(plan.expected[j] == s.raw[1][j]);
        controls_accept(&s, &plan);
        control_request_t values[CONTROL_VALUES];
        assert(controls_values(&s, "stomp", values) == 2);
        assert(!strcmp(values[0].parameter, "sensitivity") && values[0].type == C_FLOAT);
        assert(values[0].number >= 0 && values[0].number <= 1);
        assert(controls_plan(&s, &values[0], &plan) && !plan.count);
    }
    assert(previous == 2);
    r = request("stomp", "sensitivity", .5f);
    assert(controls_plan(&s, &r, &plan) && plan.expected[34] == 1);
    controls_accept(&s, &plan);

    // Level maps directly, and fixed compressor controls remain untouched.
    for (unsigned i = 0; i < 256; i++) {
        r = request("stomp", "outputLevel", (float)i / 255);
        assert(controls_plan(&s, &r, &plan) && plan.expected[32] == i);
        for (unsigned j = 16; j < 64; j++)
            if (j != 32) assert(plan.expected[j] == s.raw[1][j]);
        controls_accept(&s, &plan);
    }
    char json[4096];
    assert(controls_json(&s, json, sizeof(json)) && strstr(json, "DUBS_Sustain"));
    assert(strstr(json, "\"sensitivity\":") && strstr(json, "\"outputLevel\":1"));
    const float invalid[] = {-1, 1.01f, NAN, INFINITY};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        r = request("stomp", "sensitivity", invalid[i]);
        assert(!controls_plan(&s, &r, &plan));
    }
    r = request("stomp", "sensitivity", .5f); r.type = C_STRING;
    assert(!controls_plan(&s, &r, &plan));
    r = request("stomp", "threshold", .5f);
    assert(!controls_plan(&s, &r, &plan));

    // Edits while bypassed return with the retained compressor on enable.
    r = request("stomp", "bypass", 1); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    r = request("stomp", "sensitivity", 1);
    assert(controls_plan(&s, &r, &plan) && !plan.count); controls_accept(&s, &plan);
    r = request("stomp", "outputLevel", .25f);
    assert(controls_plan(&s, &r, &plan) && !plan.count); controls_accept(&s, &plan);
    r = request("stomp", "bypass", 0); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan) && plan.count == 1);
    assert(plan.expected[16] == 7 && plan.expected[32] == 64 && plan.expected[34] == 2);
    assert(plan.expected[33] == 64 && plan.expected[35] == 13 && plan.expected[36] == 128);
}

static void metal_gate(void) {
    probe_t p = capture(); control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    const uint8_t original[] = {s.raw[0][41], s.raw[0][47], s.raw[0][48]};
    uint8_t amp[64]; memcpy(amp, s.raw[0], 64);
    control_request_t r = {.node = "stomp", .text = "DUBS_ChromeGate", .replace = true};
    assert(controls_plan(&s, &r, &plan) && plan.gate_update && plan.count == 1);
    assert(plan.module == 1 && !plan.packets[0][16] && !plan.packets[0][17]);
    controls_accept(&s, &plan);
    assert(s.gate.selected && !s.gate.bypassed && s.gate.threshold == 7);
    assert(!s.raw[1][16] && !s.latent[1][16]);
    assert(s.raw[0][41] == 255 && s.raw[0][47] == 5 && s.raw[0][48] == 7);
    assert(!strcmp(controls_model(&s, 1), "DUBS_ChromeGate"));
    for (unsigned i = 16; i < 64; i++)
        if (i != 41 && i != 47 && i != 48) assert(s.raw[0][i] == amp[i]);
    char json[4096]; assert(controls_json(&s, json, sizeof(json)));
    assert(strstr(json, "DUBS_ChromeGate") && strstr(json, "\"threshold\":-54"));

    // Every LT value is accepted; Mustang steps stop at -42 dB, not LT's -30 dB.
    for (int db = -70; db <= -30; db++) {
        r = request("stomp", "threshold", db);
        assert(controls_plan(&s, &r, &plan) && plan.gate_update && !plan.count);
        controls_accept(&s, &plan);
        unsigned step = (unsigned)lroundf((fminf(db, -42) + 96) / 6);
        assert(s.raw[0][48] == step && s.gate.threshold == step && step <= 9);
    }
    control_request_t values[CONTROL_VALUES];
    assert(controls_values(&s, "stomp", values) == 1);
    assert(!strcmp(values[0].parameter, "threshold") && values[0].number == -42);
    const float invalid[] = {NAN, INFINITY, -71, -29};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        control_state_t before = s;
        r = request("stomp", "threshold", invalid[i]);
        assert(!controls_plan(&s, &r, &plan) && !memcmp(&s, &before, sizeof(s)));
    }

    // Bypass turns off the amplifier gate but retains the selected threshold.
    r = request("stomp", "bypass", 1); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    assert(s.gate.bypassed && s.raw[0][47] == 0 && s.raw[0][48] == 0);
    r = request("stomp", "threshold", -60);
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    assert(s.gate.threshold == 6 && s.raw[0][47] == 0 && s.raw[0][48] == 0);
    r = request("stomp", "bypass", 0); r.type = C_BOOL;
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    assert(!s.gate.bypassed && s.raw[0][47] == 5 && s.raw[0][48] == 6);
    r = (control_request_t){.node = "stomp", .parameter = "bypassType", .type = C_STRING, .text = "Post"};
    assert(!controls_plan(&s, &r, &plan));

    // Native amplifier model changes preserve the active gate and its restore values.
    s.available[0][s.available_count[0]++] = 94;
    r = (control_request_t){.node = "amp", .text = "DUBS_Jcm800", .replace = true};
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    assert(s.gate.selected && s.raw[0][47] == 5 && s.raw[0][48] == 6);
    r = (control_request_t){.node = "stomp", .text = "DUBS_Greenbox", .replace = true};
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    assert(!s.gate.selected && s.raw[1][16] == 186);
    assert(s.raw[0][41] == original[0] && s.raw[0][47] == original[1] && s.raw[0][48] == original[2]);

    // Native threshold reports update the virtual control without another write.
    r = (control_request_t){.node = "stomp", .text = "DUBS_ChromeGate", .replace = true};
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    control_event_t event = {0}; unsigned changed;
    uint8_t threshold[64] = {5, 1, 2, 94, 0, 16, 16, 0, 0, 0, 8};
    assert(controls_event(&s, &event, threshold, 64, &changed) == E_CHANGED);
    assert(changed == 3 && s.gate.threshold == 8);
    assert(controls_values(&s, "stomp", values) == 1 && values[0].number == -48);

    // A physical effect selection removes the virtual view without writing to Mustang.
    uint8_t effect[64]; memcpy(effect, p.reports[52], 64); effect[7] = 1;
    assert(controls_event(&s, &event, effect, 64, &changed) == E_WAIT);
    uint8_t end[64] = {0x1c, 1, 0, 0, 13, 0, 1, 1};
    assert(controls_event(&s, &event, end, 64, &changed) == E_CHANGED);
    assert(!s.gate.selected && !strcmp(controls_model(&s, 1), "DUBS_Greenbox"));

    // Off restores the previous amplifier gate, and a new preset discards the virtual view.
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    r = (control_request_t){.node = "stomp", .text = "DUBS_Passthru", .replace = true};
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    assert(!s.gate.selected && !s.raw[1][16]);
    r = (control_request_t){.node = "stomp", .text = "DUBS_ChromeGate", .replace = true};
    assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
    for (unsigned m = 0; m < CONTROL_MODULES; m++) p.reports[51 + m][4] = 14;
    assert(controls_adopt(&s, &p) && !s.gate.selected);
}

static void new_effects(void) {
    probe_t p = capture(); control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    const unsigned modules[] = {2, 3};
    const uint16_t ids[] = {245, 70};
    const char *names[] = {"DUBS_EcFilter", "DUBS_ReverseDelay"};
    const uint8_t defaults[][6] = {{237, 129, 7, 255, 0, 0}, {255, 129, 129, 129, 129, 0}};
    for (unsigned i = 0; i < 2; i++) {
        unsigned m = modules[i];
        control_request_t r = {.replace = true};
        snprintf(r.node, sizeof(r.node), "%s", controls_node(m));
        snprintf(r.text, sizeof(r.text), "%s", names[i]);
        assert(!controls_plan(&s, &r, &plan));
        s.available[m][s.available_count[m]++] = ids[i];
        assert(controls_plan(&s, &r, &plan) && plan.count == 2);
        assert(!plan.packets[0][16] && plan.packets[1][16] == ids[i]);
        assert(plan.expected[18] == s.raw[m][18] && plan.expected[19] == (i ? 2 : 1));
        assert(plan.expected[20] == (i ? 1 : 8) && plan.expected[21] == 1);
        assert(!memcmp(plan.expected + 32, defaults[i], 6));
        controls_accept(&s, &plan);
        assert(!strcmp(controls_model(&s, m), names[i]));

        // Only known control matches may change the native effect's defaults.
        r = request(controls_node(m), "level", .25f);
        assert(controls_plan(&s, &r, &plan) && plan.count == 1);
        assert(plan.expected[32] == 64);
        assert(!memcmp(plan.expected + 33, defaults[i] + 1, 5));
        controls_accept(&s, &plan);
        r = request(controls_node(m), i ? "feedback" : "thresh", .5f);
        assert(!controls_plan(&s, &r, &plan));
        r = request(controls_node(m), "bypass", 1); r.type = C_BOOL;
        assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
        assert(!s.raw[m][16] && s.latent[m][16] == ids[i]);
        r.number = 0;
        assert(controls_plan(&s, &r, &plan)); controls_accept(&s, &plan);
        assert(s.raw[m][16] == ids[i] && s.raw[m][32] == 64);
    }
    control_request_t r = request("delay", "time", .4f);
    assert(controls_plan(&s, &r, &plan) && plan.expected[33] == 87);
    assert(!memcmp(plan.expected + 34, defaults[1] + 2, 4));
    controls_accept(&s, &plan);
    char json[4096];
    assert(controls_json(&s, json, sizeof(json)));
    assert(strstr(json, "DUBS_EcFilter") && strstr(json, "DUBS_ReverseDelay"));
    control_request_t values[CONTROL_VALUES];
    assert(controls_values(&s, "mod", values) == 1 && !strcmp(values[0].parameter, "level"));
    assert(controls_values(&s, "delay", values) == 2 && !strcmp(values[1].parameter, "time"));
}

static void empty_slots(void) {
    probe_t p = capture();
    p.reports[53][16] = 0; p.reports[53][17] = 0; p.reports[53][18] = 5;
    p.reports[54][16] = 0; p.reports[54][17] = 0; p.reports[54][18] = 6;
    control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    const char *path[7]; assert(controls_path(&s, path));
    assert(!strcmp(path[2], "mod") && !strcmp(path[3], "amp") && !strcmp(path[4], "delay"));
    control_request_t r = {.node = "mod", .text = "DUBS_ChorusTriangle", .replace = true};
    assert(controls_plan(&s, &r, &plan) && plan.count == 1 && plan.expected[18] == 1);
    r = (control_request_t){.node = "delay", .text = "DUBS_MonoDelay", .replace = true};
    assert(controls_plan(&s, &r, &plan) && plan.count == 1 && plan.expected[18] == 4);

    // A collision must not silently put the new modulation before the stomp.
    p.reports[52][18] = 1;
    assert(controls_adopt(&s, &p));
    r = (control_request_t){.node = "mod", .text = "DUBS_ChorusTriangle", .replace = true};
    assert(!controls_plan(&s, &r, &plan) && !plan.count);
}

static void recorded(const char *path) {
    FILE *file = fopen(path, "r"); assert(file);
    probe_t p = {.phase = P_DONE, .writes = 3};
    char line[256];
    while (fgets(line, sizeof(line), file)) {
        if (strncmp(line, "RX report: ", 11)) continue;
        assert(p.count < PROBE_REPORTS);
        const char *cursor = line + 11;
        for (unsigned j = 0; j < 64; j++) {
            unsigned value; int length;
            assert(sscanf(cursor, "%2x%n", &value, &length) == 1);
            p.reports[p.count][j] = value; cursor += length;
        }
        p.count++;
    }
    fclose(file);
    control_state_t s = {0}; control_plan_t plan;
    assert(controls_adopt(&s, &p));
    char json[4096]; assert(controls_json(&s, json, sizeof(json)));
    // The recorded compressor has no LT menu counterpart; gain must still work.
    assert(strstr(json, "\"torinoNativeModel\":7"));
    control_request_t r = request("amp", "gain", .25f);
    assert(controls_plan(&s, &r, &plan) && plan.expected[33] == 64);
    r = (control_request_t){.node = "mod", .text = "DUBS_ChorusTriangle", .replace = true};
    assert(controls_plan(&s, &r, &plan) && plan.expected[18] == 1);

    // The real Twin snapshot uses Low gate with depth 0 and threshold 2.
    // Switching amp models must not replace that tuple with 128/0 defaults.
    assert(s.raw[0][41] == 0 && s.raw[0][47] == 1 && s.raw[0][48] == 2);
    const char *amps[] = {"DUBS_Champ57", "DUBS_Ac30Tb"};
    for (unsigned i = 0; i < 2; i++) {
        r = (control_request_t){.node = "amp", .replace = true};
        snprintf(r.text, sizeof(r.text), "%s", amps[i]);
        assert(controls_plan(&s, &r, &plan) && plan.count == 1);
        assert(plan.expected[41] == 0 && plan.expected[47] == 1 && plan.expected[48] == 2);
        assert(plan.packets[0][41] == 0 && plan.packets[0][47] == 1 && plan.packets[0][48] == 2);
    }
}

static void cached_intent(void) {
    probe_t p = capture();
    control_state_t actual = {0};
    control_plan_t plan, sent;
    assert(controls_adopt(&actual, &p));
    control_state_t wanted = actual;

    // Busy-time knobs collapse into one module packet with all latest values.
    control_request_t r = request("amp", "gain", .3f);
    assert(controls_plan(&wanted, &r, &plan));
    controls_accept(&wanted, &plan);
    assert(controls_diff(&wanted, &actual, 0, &sent));
    r.number = .8f;
    assert(controls_plan(&wanted, &r, &plan));
    controls_accept(&wanted, &plan);
    r = request("amp", "bass", .6f);
    assert(controls_plan(&wanted, &r, &plan));
    controls_accept(&wanted, &plan);
    controls_accept(&actual, &sent);
    assert(controls_diff(&wanted, &actual, 0, &plan));
    assert(plan.count == 1 && plan.expected[33] == 204 && plan.expected[38] == 153);
    controls_accept(&actual, &plan);
    assert(!controls_diff(&wanted, &actual, 0, &plan));

    // Bypass drops unsent wire edits, but keeps their parameters for re-enable.
    r = request("stomp", "gain", .7f);
    assert(controls_plan(&wanted, &r, &plan));
    controls_accept(&wanted, &plan);
    uint8_t latent[64]; memcpy(latent, wanted.raw[1], 64);
    r = request("stomp", "bypass", 1); r.type = C_BOOL;
    assert(controls_plan(&wanted, &r, &plan));
    controls_accept(&wanted, &plan);
    assert(controls_diff(&wanted, &actual, 1, &plan));
    assert(plan.count == 1 && !plan.packets[0][16] && !plan.packets[0][17]);
    controls_accept(&actual, &plan);
    assert(!memcmp(wanted.latent[1] + 16, latent + 16, 48));
    r.number = 0;
    assert(controls_plan(&wanted, &r, &plan));
    controls_accept(&wanted, &plan);
    assert(controls_diff(&wanted, &actual, 1, &plan));
    assert(!memcmp(plan.expected + 16, latent + 16, 48));

    // Recovery computes differences without replacing the requested cache.
    control_state_t saved = wanted, readback = {0};
    assert(controls_adopt(&readback, &p));
    assert(controls_diff(&wanted, &readback, 0, &plan));
    assert(!memcmp(&wanted, &saved, sizeof(wanted)));
    r = request("amp", "gain", NAN);
    assert(!controls_plan(&wanted, &r, &plan));
    assert(!memcmp(&wanted, &saved, sizeof(wanted)));

    // Model replacement drops old-model knobs instead of flushing them.
    wanted.available[0][wanted.available_count[0]++] = 94;
    r = (control_request_t){.node = "amp", .text = "DUBS_Jcm800", .replace = true};
    assert(controls_plan(&wanted, &r, &plan));
    uint8_t defaults[48]; memcpy(defaults, plan.expected + 16, 48);
    controls_accept(&wanted, &plan);
    assert(controls_diff(&wanted, &actual, 0, &plan));
    assert(plan.expected[16] == 94 && !memcmp(plan.expected + 16, defaults, 48));
    assert(plan.expected[33] != 204);
}

static void physical_packets(void) {
    probe_t p = capture();
    control_state_t s = {0};
    control_event_t event = {0};
    unsigned changed;
    assert(controls_adopt(&s, &p));
    uint8_t knob[64] = {5, 1, 2, 117, 0, 1, 1, 12, 0, 0, 119};
    assert(controls_event(&s, &event, knob, 64, &changed) == E_CHANGED);
    assert(changed == 1 && s.raw[0][33] == 119 && s.latent[0][33] == 119);
    const unsigned indices[] = {0, 4, 6};
    for (unsigned i = 0; i < 3; i++) {
        knob[5] = knob[6] = indices[i]; knob[10] = 80 + i;
        assert(controls_event(&s, &event, knob, 64, &changed) == E_CHANGED);
        assert(s.raw[0][32 + indices[i]] == 80 + i);
    }
    control_state_t before = s;
    knob[11] = 1;
    assert(controls_event(&s, &event, knob, 64, &changed) == E_BAD);
    assert(!memcmp(&s, &before, sizeof(s)));
    knob[11] = 0; knob[3] = 100;
    assert(controls_event(&s, &event, knob, 64, &changed) == E_BAD);

    // A selector bundle cannot publish half an effect change.
    uint8_t effect[64]; memcpy(effect, s.raw[2], 64);
    effect[16] = 25;
    assert(controls_event(&s, &event, effect, 64, &changed) == E_WAIT);
    assert(!memcmp(&s, &before, sizeof(s)));
    uint8_t end[64] = {0x1c, 1, 0, 0, 13, 0, 1, 1};
    assert(controls_event(&s, &event, end, 64, &changed) == E_CHANGED);
    assert(changed == 4 && s.raw[2][16] == 25);
    effect[16] = 0;
    assert(controls_event(&s, &event, effect, 64, &changed) == E_WAIT);
    assert(controls_event(&s, &event, end, 64, &changed) == E_CHANGED);
    assert(!s.latent[2][16] && !strcmp(controls_model(&s, 2), "DUBS_Passthru"));

    // Reject partial preset switches without replacing any requested settings.
    before = s;
    effect[4] = 14;
    assert(controls_event(&s, &event, effect, 64, &changed) == E_WAIT);
    end[4] = 14;
    assert(controls_event(&s, &event, end, 64, &changed) == E_BAD);
    assert(!memcmp(&s, &before, sizeof(s)));
}

static void replay_events(const char *path) {
    FILE *f = fopen(path, "r"); assert(f);
    probe_t p = capture();
    // Only the initial amp is needed to decode the captured incremental knobs.
    for (unsigned m = 1; m < CONTROL_MODULES; m++) p.reports[51 + m][16] = p.reports[51 + m][17] = 0;
    control_state_t s = {0}; control_event_t event = {0};
    assert(controls_adopt(&s, &p));
    char line[2048]; unsigned packets = 0, updates = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "NATIVE ", 7)) continue;
        uint8_t raw[64];
        for (unsigned i = 0; i < 64; i++) {
            unsigned byte;
            assert(sscanf(line + 7 + 2*i, "%2x", &byte) == 1);
            raw[i] = byte;
        }
        unsigned changed;
        enum control_event result = controls_event(&s, &event, raw, 64, &changed);
        if (result == E_BAD) fprintf(stderr, "Rejected physical packet %u: %s", packets, line);
        assert(result != E_BAD);
        packets++; updates += result == E_CHANGED;
    }
    fclose(f);
    assert(packets && updates && !event.mask && !event.preset);
    printf("physical replay: %u packets, %u direct cache updates, no settings reads: %s\n",
           packets, updates, path);
}

int main(int argc, char **argv) {
    physical_packets();
    if (argc == 3 && !strcmp(argv[1], "--events")) replay_events(argv[2]);
    cached_intent();
    parameters(); round_trips(); bypass(); models(); bounds(); catalog(); vari_fuzz(); sustain(); metal_gate(); new_effects(); empty_slots();
    if (argc == 2) recorded(argv[1]);
    puts("controls: parameter payloads, model layouts, bypass retention, capabilities, and bounds passed");
}
