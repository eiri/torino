#include "probe.h"
#include <string.h>

bool probe_target(uint16_t vid, uint16_t pid, uint8_t interface,
                  const uint8_t *descriptor, size_t length) {
    static const uint8_t expected[] = {
        0x06, 0x00, 0xff, 0x09, 0x01, 0xa1, 0x01, 0x09,
        0x02, 0x15, 0x80, 0x25, 0x7f, 0x95, 0x40, 0x75,
        0x08, 0x81, 0x02, 0x09, 0x03, 0x15, 0x80, 0x25,
        0x7f, 0x95, 0x40, 0x75, 0x08, 0x91, 0x02, 0xc0,
    };
    return vid == 0x1ed8 && pid == 0x0014 && interface == 0 && descriptor &&
           length == sizeof(expected) && memcmp(descriptor, expected, length) == 0;
}

void probe_fail(probe_t *probe, const char *reason) {
    probe->phase = P_FAILED;
    probe->error = reason;
}

bool probe_start(probe_t *probe) {
    if (probe->phase != P_IDLE) return false;
    probe->phase = P_HELLO;
    return true;
}

bool probe_refresh(probe_t *probe, const probe_t *baseline) {
    if (probe == baseline || probe->phase != P_IDLE || baseline->phase != P_DONE ||
        baseline->pending || baseline->count != PROBE_REPORTS || baseline->writes != 3) return false;

    memcpy(probe->reports, baseline->reports, 2 * PROBE_SIZE);
    probe->count = 2;
    probe->writes = 2;
    probe->phase = P_SETTINGS;
    return true;
}

bool probe_selected(probe_t *out, const probe_t *stream, const probe_t *baseline) {
    static const uint8_t tags[] = {4, 5, 6, 7, 8, 9, 13, 0};
    if (out == stream || out == baseline || stream->phase != P_SETTINGS ||
        stream->pending || stream->writes != 3 || stream->count != 58 ||
        baseline->phase != P_DONE || baseline->pending ||
        baseline->count != PROBE_REPORTS || baseline->writes != 3) return false;

    for (unsigned i = 0; i < sizeof(tags); i++) {
        const uint8_t *r = stream->reports[50 + i];
        if (r[0] != 0x1c || r[1] != 1 || r[2] != tags[i] || r[3] ||
            r[4] >= 24 || r[4] != stream->reports[50][4]) return false;
    }
    *out = *baseline;
    memcpy(out->reports, stream->reports, 58 * PROBE_SIZE);
    return true;
}

bool probe_next(probe_t *probe, uint8_t packet[PROBE_SIZE]) {
    static const uint8_t prefixes[][2] = {{0x00, 0xc3}, {0x1a, 0xc1}, {0xff, 0xc1}};
    if (probe->phase < P_HELLO || probe->phase > P_SETTINGS || probe->pending ||
        probe->writes != (unsigned)probe->phase - 1) return false;
    memset(packet, 0, PROBE_SIZE);
    memcpy(packet, prefixes[probe->writes], 2);
#ifdef TORINO_CURRENT_READ
    if (probe->phase == P_SETTINGS) packet[1] = 0xc5;
#endif
    probe->writes++;
    probe->pending = true;
    return true;
}

void probe_sent(probe_t *probe, size_t length) {
    if (!probe->pending || length != PROBE_SIZE) {
        probe_fail(probe, "output-completion");
        return;
    }
    probe->pending = false;
}

static bool current_block(const probe_t *probe) {
    static const uint8_t tags[] = {4, 5, 6, 7, 8, 9, 0x0d, 0};
    unsigned matches = 0;
    for (size_t i = 2; i + sizeof(tags) <= probe->count; i++) {
        bool match = true;
        for (size_t j = 0; j < sizeof(tags); j++) {
            const uint8_t *p = probe->reports[i + j];
            match = match && p[0] == 0x1c && p[1] == 1 && p[2] == tags[j] &&
                    p[3] == 0 && p[4] < 24 && p[4] == probe->reports[i][4];
        }
        matches += match;
    }
    return matches == 1;
}

void probe_receive(probe_t *probe, const uint8_t *packet, size_t length) {
    if (length != PROBE_SIZE || !packet || probe->phase < P_HELLO ||
        probe->phase > P_SETTINGS || probe->writes != (unsigned)probe->phase ||
        probe->count >= PROBE_REPORTS) {
        probe_fail(probe, "unexpected-input");
        return;
    }
    if ((probe->phase == P_HELLO && memcmp(packet, "\0\0\0\0", 4) != 0) ||
        (probe->phase == P_INIT && memcmp(packet, "\1\0\2\1", 4) != 0)) {
        probe_fail(probe, "identity-reply");
        return;
    }
    memcpy(probe->reports[probe->count++], packet, PROBE_SIZE);
    if (probe->phase == P_HELLO) {
        probe->phase = P_INIT;
    } else if (probe->phase == P_INIT) {
        probe->phase = P_SETTINGS;
    } else if (packet[0] == 0xff && packet[1] == 1) {
        if (probe->count != PROBE_REPORTS || probe->pending || !current_block(probe)) {
            probe_fail(probe, "incomplete-settings");
        } else {
            probe->phase = P_DONE;
        }
    } else if (probe->count == PROBE_REPORTS) {
        probe_fail(probe, "missing-end");
    }
}
