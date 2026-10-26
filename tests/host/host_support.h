#pragma once
#include <stdint.h>
extern uint32_t host_time;
extern int host_fail_save;
void host_clear_storage(void);
void host_legacy_storage(unsigned count);
void host_corrupt_storage(void);
