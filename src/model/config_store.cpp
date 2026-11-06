#include "model/config_store.h"
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// Entries are heap strings; a typical config is a few hundred lines (~30 KB),
// well inside the panel's PSRAM. Capped so a runaway dump cannot eat it all.
static const size_t CFG_MAX = 1500;

struct entry_t {
    std::string path, value;
};
static std::vector<entry_t> s_cfg;
static uint32_t             s_gen = 0;

// Open ancestors by indent: s_stack[d] is the key at nesting depth d.
static const int DEPTH_MAX = 8;
static std::string s_stack[DEPTH_MAX];

void cfg_clear() {
    s_cfg.clear();
    for (auto& s : s_stack) s.clear();
    s_gen++;
}

bool cfg_feed_line(const char* line) {
    if (!line) return false;
    int ind = 0;
    while (line[ind] == ' ') ind++;
    const char* k = line + ind;
    if (!*k || *k == '#' || *k == '-' || *k == '[' || *k == '$' || *k == '<' || *k == '{') return false;
    const char* colon = strchr(k, ':');
    if (!colon || colon == k) return false;
    // "ok" / "error:3" etc. are not config
    if (strncmp(k, "error", 5) == 0 || strcmp(k, "ok") == 0) return false;

    int depth = ind / 2;
    if (depth >= DEPTH_MAX) return false;

    std::string key(k, (size_t)(colon - k));
    s_stack[depth] = key;
    for (int d = depth + 1; d < DEPTH_MAX; d++) s_stack[d].clear();

    const char* v = colon + 1;
    while (*v == ' ') v++;
    std::string val(v);
    while (!val.empty() && (val.back() == ' ' || val.back() == '\r')) val.pop_back();
    // YAML trailing comment ("15.9  # note") is not part of the value.
    size_t hash = val.find(" #");
    if (hash != std::string::npos) {
        val.erase(hash);
        while (!val.empty() && val.back() == ' ') val.pop_back();
    }

    std::string path;
    for (int d = 0; d <= depth; d++) {
        if (d) path += '/';
        path += s_stack[d];
    }
    if (s_cfg.size() >= CFG_MAX) return false;
    s_cfg.push_back({path, val});
    s_gen++;
    return true;
}

const char* cfg_get(const char* path) {
    if (!path) return nullptr;
    for (const auto& e : s_cfg)
        if (e.path == path) return e.value.c_str();
    return nullptr;
}

float cfg_float(const char* path, float fallback) {
    const char* v = cfg_get(path);
    if (!v || !*v) return fallback;
    char* end = nullptr;
    float f   = strtof(v, &end);
    return end == v ? fallback : f;
}

size_t      cfg_count()            { return s_cfg.size(); }
const char* cfg_path_at(size_t i)  { return i < s_cfg.size() ? s_cfg[i].path.c_str() : nullptr; }
const char* cfg_value_at(size_t i) { return i < s_cfg.size() ? s_cfg[i].value.c_str() : nullptr; }
uint32_t    cfg_generation()       { return s_gen; }
