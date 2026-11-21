#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Machine settings kept in NVS (namespace "elk") */
enum { DISK_NONE, DISK_ADFS, DISK_DFS };

typedef struct {
    uint8_t disk_expansion;
    uint8_t fast_tape;
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);
