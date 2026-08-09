#pragma once
#include <stdint.h>
#include <stddef.h>

// FluidNC's live settings, as reported by $S (all) or $SC (changed only).
// Each reply line is "$<name>=<value>", values URI-encoded for characters Grbl
// would otherwise choke on (ProcessSettings.cpp:125).
//
// Editing these beats rewriting config.yaml: most take effect immediately, and
// a bad value is rejected with an error instead of leaving the machine with a
// config file it cannot parse.
#define FS_MAX      160
#define FS_NAME_MAX 48
#define FS_VAL_MAX  32

struct fnc_setting_t {
    char name[FS_NAME_MAX];
    char value[FS_VAL_MAX];
};

void   fncset_clear();
void   fncset_feed_line(const char* line);   // returns quietly for non-settings
size_t fncset_count();
const fnc_setting_t* fncset_at(size_t i);
bool   fncset_dirty_take();

void fncset_request_all();
void fncset_request_changed();
void fncset_write(const char* name, const char* value);
