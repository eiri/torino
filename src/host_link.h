#pragma once
#include "probe.h"
#include <stdatomic.h>

enum link_state {
    L_WAIT, L_READY, L_READING, L_DONE, L_FAILED, L_WRITING, L_WRITE_DONE, L_CURRENT,
    L_RECOVERY_DONE = 9, L_RECOVERING, L_OFFLINE
};
extern atomic_uint link, heartbeat;
extern atomic_bool requested, cancelled;
extern probe_t probe;
#ifdef TORINO_RELAY
extern atomic_uint gain_requested, gain_confirmed, capture_index;
extern atomic_bool gain_live, confirm_requested;
extern atomic_bool reconnect_ack, write_in_flight, recover_requested, recovery_started;
#include "native.h"
extern native_queue_t native_reports, native_events;
extern atomic_uint idle_count, idle_header, settings_reads;

typedef struct {
    uint32_t cycle, report, command_us, sent_us, received_us;
    uint32_t rearm_us, gap_us, loop_us, poll;
    uint32_t reserved[7];
} read_trace_t;
_Static_assert(sizeof(read_trace_t) == 64, "timing record size");
extern native_queue_t read_traces;
extern atomic_uint trace_drops;
extern atomic_uint recovery_version, recovery_confirmed;
extern atomic_bool recovery_confirm_requested;
extern atomic_uint endpoint_timing;
void host_tune_polling(uint8_t address, uint16_t vid, uint16_t pid);
void host_discover(uint32_t now, bool idle);
void host_forget(void);
extern atomic_uint host_port, discovery_retries, host_sessions;
#include "controls.h"
extern control_plan_t control_command;
extern probe_t recovery_capture, selected_capture;
extern atomic_bool select_read, selected_ready;
extern const char *host_error;
#endif
void host_main(void);
void feed_watchdog(void);
