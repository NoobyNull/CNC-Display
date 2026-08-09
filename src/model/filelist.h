#pragma once
#include <stdint.h>
#include <stddef.h>

#define FL_MAX_ENTRIES 64
#define FL_NAME_MAX    64

struct fl_entry_t {
    char     name[FL_NAME_MAX];
    uint32_t size;
    bool     is_dir;
};

void   filelist_reset();
void   filelist_request();                    // sends $Files/ListGCode
void   filelist_feed_line(const char* line);  // consumes the JSON reply
size_t filelist_count();
const fl_entry_t* filelist_at(size_t i);
const char* filelist_path();
void   filelist_enter(const char* name);      // descend, or ".." to go up
bool   filelist_dirty_take();
