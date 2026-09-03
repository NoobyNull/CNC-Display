// The panel's local microSD, backed by the sim/sdcard/ directory.
//
// Real files on purpose: the Run page's local-file browser, the G-code path
// preview and the XModem uploader all read through this interface, so pointing
// it at a directory exercises them for real instead of stubbing them out.

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <algorithm>

#include "hw/sdcard.h"

static sd_entry_t s_entries[SD_MAX_ENTRIES];
static size_t     s_count = 0;
static FILE*      s_fp = nullptr;
static uint32_t   s_size = 0;
static char       s_root[512] = "sim/sdcard";

// Join the simulated card's root with a panel-side path. The firmware speaks
// in "/" and "/subdir"; neither may escape the root.
static void real_path(const char* path, char* out, size_t n) {
    const char* p = (path && *path) ? path : "/";
    while (*p == '/') p++;
    if (strstr(p, "..")) p = "";
    snprintf(out, n, "%s/%s", s_root, p);
}

void sd_set_root(const char* root) {
    if (root && *root) { strncpy(s_root, root, sizeof(s_root) - 1); s_root[sizeof(s_root) - 1] = 0; }
}

bool sd_begin() {
    struct stat st;
    return stat(s_root, &st) == 0 && S_ISDIR(st.st_mode);
}

bool sd_present() { return sd_begin(); }

bool sd_list(const char* path) {
    s_count = 0;
    char dir[600];
    real_path(path, dir, sizeof(dir));
    DIR* d = opendir(dir);
    if (!d) return false;

    struct dirent* e;
    while ((e = readdir(d)) && s_count < SD_MAX_ENTRIES) {
        if (e->d_name[0] == '.') continue;      // no dotfiles, no . or ..
        char full[1200];
        snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;

        sd_entry_t& x = s_entries[s_count++];
        strncpy(x.name, e->d_name, SD_NAME_MAX - 1);
        x.name[SD_NAME_MAX - 1] = 0;
        x.is_dir = S_ISDIR(st.st_mode);
        x.size   = x.is_dir ? 0u : (uint32_t)st.st_size;
    }
    closedir(d);

    // readdir order is filesystem order, which on the real card is roughly
    // creation order; sorting keeps the browser stable between runs.
    std::sort(s_entries, s_entries + s_count, [](const sd_entry_t& a, const sd_entry_t& b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return strcasecmp(a.name, b.name) < 0;
    });
    return true;
}

size_t            sd_count() { return s_count; }
const sd_entry_t* sd_at(size_t i) { return i < s_count ? &s_entries[i] : nullptr; }

bool sd_open(const char* path) {
    sd_close();
    char full[600];
    real_path(path, full, sizeof(full));
    s_fp = fopen(full, "rb");
    if (!s_fp) return false;
    fseek(s_fp, 0, SEEK_END);
    s_size = (uint32_t)ftell(s_fp);
    fseek(s_fp, 0, SEEK_SET);
    return true;
}

size_t sd_read(uint8_t* dst, size_t max) {
    if (!s_fp || !dst || !max) return 0;
    return fread(dst, 1, max, s_fp);
}

void sd_close() {
    if (s_fp) { fclose(s_fp); s_fp = nullptr; }
    s_size = 0;
}

uint32_t sd_size() { return s_size; }
