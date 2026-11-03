/* stub/picocalc_shell.h -- no shell clock on Linux (numbered names) */
#pragma once
#include <stdbool.h>
#include <stdint.h>
static inline bool picocalc_shell_time(int64_t *ms, int *off) { (void)ms; (void)off; return false; }
