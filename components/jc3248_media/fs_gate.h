#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// One mutex protects every FatFS access (browser, player and the HTTP upload
// server), and one flag parks all of them while the USB host owns the card or
// while an upload rewrites a file.
void fs_gate_init(void);
void fs_gate_set_suspend(bool suspended);
bool fs_gate_suspended(void);
bool fs_gate_lock(uint32_t timeout_ms);
void fs_gate_unlock(void);

#ifdef __cplusplus
}
#endif
