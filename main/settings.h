#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Machine settings kept in NVS (namespace "elk") */
typedef struct {
    uint8_t plus3;          /* 0: none, 1: an Acorn Plus 3 (ADFS, a 3.5" drive) */
    uint8_t fast_tape;
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);
