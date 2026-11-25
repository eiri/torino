#include "preset.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char json[6144];
static control_state_t base, draft;

static void fixture(const char *amp, const char *params, const char *name) {
    base = (control_state_t){.valid = true};
    const unsigned ids[] = {117, 186, 19, 43, 11};
    for (unsigned m = 0; m < CONTROL_MODULES; m++) {
        uint8_t *r = base.raw[m];
        r[0] = 0x1c; r[1] = 1; r[2] = 5 + m; r[4] = 13; r[6] = 1;
        r[16] = ids[m]; r[17] = ids[m] >> 8; r[18] = m - 1;
        base.available_count[m] = 1; base.available[m][0] = ids[m];
    }
    base.available[0][base.available_count[0]++] = 241;
    memcpy(base.latent, base.raw, sizeof(base.raw));
    int n = snprintf(json, sizeof(json),
        "{\"nodeType\":\"preset\",\"info\":{\"product_id\":\"mustang-lt\",\"displayName\":\"%s\"},"
        "\"audioGraph\":{\"nodes\":["
        "{\"nodeId\":\"amp\",\"nodeType\":\"dspUnit\",\"FenderId\":\"%s\",\"dspUnitParameters\":{%s}},"
        "{\"nodeId\":\"stomp\",\"nodeType\":\"dspUnit\",\"FenderId\":\"DUBS_Greenbox\","
        "\"dspUnitParameters\":{\"gain\":0.8,\"level\":-10,\"bypass\":true}},"
        "{\"nodeId\":\"mod\",\"nodeType\":\"dspUnit\",\"FenderId\":\"DUBS_Passthru\",\"dspUnitParameters\":{\"bypass\":false,\"bypassType\":\"Post\"}},"
        "{\"nodeId\":\"delay\",\"nodeType\":\"dspUnit\",\"FenderId\":\"DUBS_TapeDelayLite\","
        "\"dspUnitParameters\":{\"dlyTime\":0.5,\"bypassType\":\"Post\",\"bypass\":false,"
        "\"tapTimeBPM\":120,\"noteDivision\":\"off\",\"stereoSpread\":0}},"
        "{\"nodeId\":\"reverb\",\"nodeType\":\"dspUnit\",\"FenderId\":\"DUBS_Spring65\","
        "\"dspUnitParameters\":{\"wetLvl\":0.4,\"decay\":0.5}}],\"connections\":[",
        name, amp, params);
    assert(n > 0 && (size_t)n < sizeof(json));
    size_t used = n;
    const char *path[] = {"preset", "stomp", "mod", "amp", "delay", "reverb", "preset"};
    for (unsigned i = 0; i < 6; i++) for (unsigned ch = 0; ch < 2; ch++) {
        n = snprintf(json + used, sizeof(json) - used,
            "%s{\"input\":{\"index\":%u,\"nodeId\":\"%s\"},\"output\":{\"index\":%u,\"nodeId\":\"%s\"}}",
            i || ch ? "," : "", ch, path[i], ch, path[i + 1]);
        assert(n > 0 && (size_t)n < sizeof(json) - used);
        used += n;
    }
    strcpy(json + used, "]}}");
}

int main(void) {
    const char *params = "\"gain\":0.3,\"volume\":-12,\"gatePreset\":\"off\",\"gateDetectorPosition\":\"jack\"";
    fixture("DUBS_Twin65", params, "Factory \\\"Clean\\\"");
    control_state_t saved = base;
    char name[33];
    assert(preset_decode(json, strlen(json), &base, &draft, name));
    assert(!strcmp(name, "Factory \"Clean\""));
    assert(!memcmp(&base, &saved, sizeof(base)));
    assert(draft.raw[0][33] == 77 && draft.raw[0][47] == 0 && draft.raw[0][41] == 255);
    assert(!draft.raw[1][16] && draft.latent[1][16] == 186 && draft.latent[1][33] == 204);
    assert(draft.raw[3][16] == 43 && draft.raw[3][18] == 4 && draft.raw[4][18] == 5);
    assert(!draft.raw[2][16] && draft.raw[0][7]);

    // Reject truncated/malformed JSON and duplicate parameters, not unsupported values.
    size_t length = strlen(json);
    for (size_t n = 0; n < length; n++) assert(!preset_decode(json, n, &base, &draft, name));
    const char *bad[] = {"\"gain\":0.2,\"gain\":0.4", "\"gain\":NaN", "\"gain\":01", "\"gain\":1e999"};
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        fixture("DUBS_Twin65", bad[i], "Bad");
        assert(!preset_decode(json, strlen(json), &base, &draft, name));
    }
    // Unsupported values leave model defaults, not the previous preset's knobs.
    const char *adapt[] = {"\"gain\":2", "\"gain\":\"loud\"", "\"gain\":null", "\"gain\":{}",
        "\"unknown\":0.5", "\"gateDetectorPosition\":\"output\"", "\"gatePreset\":\"alien\"",
        "\"noteDivision\":\"quarter\"", "\"tapTimeBPM\":120", "\"bypass\":2", "\"stereoSpread\":1"};
    for (unsigned i = 0; i < sizeof(adapt) / sizeof(adapt[0]); i++) {
        fixture("DUBS_Twin65", adapt[i], "Adapted");
        base.raw[0][33] = base.latent[0][33] = 250;
        base.raw[0][47] = base.latent[0][47] = 4;
        assert(preset_decode(json, strlen(json), &base, &draft, name));
        assert(draft.raw[0][16] == 117 && draft.raw[0][33] == 86);
        assert(draft.raw[0][47] == 0 && draft.raw[0][48] == 0);
    }
    const char *gates[] = {"off", "low", "mid", "high", "super"};
    for (unsigned mode = 0; mode < 5; mode++) {
        char gate[64];
        snprintf(gate, sizeof(gate), "\"gatePreset\":\"%s\"", gates[mode]);
        fixture("DUBS_Twin65", gate, "Gate preset");
        assert(preset_decode(json, strlen(json), &base, &draft, name));
        assert(draft.raw[0][47] == mode && draft.raw[0][48] == (mode ? mode + 1 : 0));
        assert(draft.raw[0][41] == (mode == 1 ? 0 : 255));
    }
    fixture("DUBS_Unknown", params, "Adapted model");
    saved = base;
    assert(preset_decode(json, strlen(json), &base, &draft, name));
    assert(draft.raw[0][16] == 241 && !strcmp(controls_model(&draft, 0), "DUBS_LinearGain"));
    assert(!memcmp(&base, &saved, sizeof(base)));

    // Known-but-unavailable models use the same fallback as unknown models.
    fixture("DUBS_Champ57", "\"unknown\":1", "Unavailable model");
    assert(preset_decode(json, strlen(json), &base, &draft, name));
    assert(draft.raw[0][16] == 241 && draft.raw[0][33] == 129);

    // Unsupported effects cannot leak source parameters or latent models into plans.
    fixture("DUBS_Twin65", params, "Adapted effect");
    char *effect = strstr(json, "DUBS_Greenbox"); assert(effect);
    memcpy(effect, "DUBS_UnknownX", strlen("DUBS_Greenbox"));
    assert(preset_decode(json, strlen(json), &base, &draft, name));
    assert(!draft.raw[1][16] && !draft.latent[1][16]);
    assert(!strcmp(controls_model(&draft, 1), "DUBS_Passthru"));

    // Valid values on a supported model still survive alongside invalid values.
    fixture("DUBS_Twin65", "\"gain\":0.3,\"treble\":2,\"unknown\":true", "Mixed");
    assert(preset_decode(json, strlen(json), &base, &draft, name));
    assert(draft.raw[0][33] == 77 && draft.raw[0][36] == 180);
    fixture("DUBS_Twin65", params, "Bad route");
    char *edge = strstr(json, "\"output\":{\"index\":0,\"nodeId\":\"stomp\"");
    assert(edge); edge[strlen("\"output\":{\"index\":")] = '1';
    assert(!preset_decode(json, strlen(json), &base, &draft, name));
    puts("preset import: model fallbacks, parameter defaults, preview isolation, routing, and bounds passed");
}
