// The panel's local microSD, backed by the sim/sdcard/ directory. The panel
// only ever reads /wifi.txt from its card (network firmware update); G-code
// comes from the controller, which fluidnc_sim.py serves from the same folder.

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "hw/sdcard.h"

static FILE*      s_fp = nullptr;
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

bool sd_open(const char* path) {
    sd_close();
    char full[600];
    real_path(path, full, sizeof(full));
    s_fp = fopen(full, "rb");
    return s_fp != nullptr;
}

size_t sd_read(uint8_t* dst, size_t max) {
    if (!s_fp || !dst || !max) return 0;
    return fread(dst, 1, max, s_fp);
}

void sd_close() {
    if (s_fp) { fclose(s_fp); s_fp = nullptr; }
}
