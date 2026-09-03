#pragma once
// In-memory stand-in for the ESP32 NVS wrapper, so src/model/settings.cpp
// compiles for the simulator untouched.
//
// Deliberately NOT persisted to disk: every simulator run should start from the
// firmware's compiled-in defaults, which is the state a freshly flashed panel
// boots in and therefore the state worth testing.

#include <map>
#include <string>
#include <string.h>
#include <stdint.h>

class Preferences {
    // One process-wide store, since the real NVS namespace outlives each
    // begin()/end() pair the way this map outlives each instance.
    static std::map<std::string, std::string>& store() {
        static std::map<std::string, std::string> s;
        return s;
    }
    std::string _ns;

    const std::string* find(const char* k) const {
        auto it = store().find(_ns + "." + k);
        return it == store().end() ? nullptr : &it->second;
    }
    void set(const char* k, const std::string& v) { store()[_ns + "." + k] = v; }

public:
    bool begin(const char* name, bool = false) { _ns = name ? name : ""; return true; }
    void end() {}
    bool clear() { store().clear(); return true; }

    bool     getBool(const char* k, bool d = false)      { auto v = find(k); return v ? *v == "1" : d; }
    uint8_t  getUChar(const char* k, uint8_t d = 0)      { auto v = find(k); return v ? (uint8_t)atoi(v->c_str()) : d; }
    uint32_t getUInt(const char* k, uint32_t d = 0)      { auto v = find(k); return v ? (uint32_t)strtoul(v->c_str(), nullptr, 10) : d; }
    float    getFloat(const char* k, float d = 0.0f)     { auto v = find(k); return v ? strtof(v->c_str(), nullptr) : d; }

    size_t getString(const char* k, char* out, size_t n) {
        auto v = find(k);
        if (!v || !out || !n) { if (out && n) out[0] = 0; return 0; }
        strncpy(out, v->c_str(), n - 1);
        out[n - 1] = 0;
        return strlen(out);
    }

    size_t putBool(const char* k, bool v)      { set(k, v ? "1" : "0"); return 1; }
    size_t putUChar(const char* k, uint8_t v)  { set(k, std::to_string((int)v)); return 1; }
    size_t putUInt(const char* k, uint32_t v)  { set(k, std::to_string(v)); return 1; }
    size_t putFloat(const char* k, float v)    { set(k, std::to_string(v)); return 1; }
    size_t putString(const char* k, const char* v) { set(k, v ? v : ""); return strlen(v ? v : ""); }
};
