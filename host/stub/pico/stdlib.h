/* stub/pico/stdlib.h -- just enough of the Pico SDK for screen.c on Linux */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef uint64_t absolute_time_t;
absolute_time_t make_timeout_time_ms(uint32_t ms);
bool time_reached(absolute_time_t t);
static inline void sleep_ms(uint32_t ms) { (void)ms; }
absolute_time_t get_absolute_time(void);
