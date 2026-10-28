#pragma once
#include <stdint.h>
#include "bt_runtime.h"
#include "app_state.h"
extern uint32_t host_time;
extern int host_fail_save;
extern int host_fail_stats;
extern unsigned host_stats_reads, host_namespace_reads;
extern size_t host_available_entries;
void host_namespace_usage(const char *name, size_t entries);
void host_clear_storage(void);
void host_legacy_storage(unsigned count);
void host_corrupt_storage(void);
void host_v1_storage(const remote_t *remote);
void host_version_storage(const remote_t *remote, unsigned version);
extern bt_snapshot_t host_bt_status;
extern unsigned host_bt_opens, host_bt_closes, host_bt_taps, host_bt_forgets, host_bt_releases;
extern bool host_bt_pair_after, host_bt_saved;
extern bt_key_report_t host_bt_report;
extern bt_key_report_t host_bt_reports[256];
extern unsigned host_bt_report_count;
extern unsigned host_adv_calls, host_adv_fail_on;
extern uint16_t host_adv_service, host_adv_appearance;
extern bool host_adv_has_name, host_scan_has_name;
