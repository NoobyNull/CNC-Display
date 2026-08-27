#pragma once
// Desktop stand-in for the ESP32 Preferences (NVS) library, for [env:sim].
//
// This exists so the REAL src/model/settings.cpp compiles unmodified — the
// alternative was a parallel sim-only settings implementation, which would
// then drift from the one the panel runs. Storage is a process-lifetime map:
// settings survive navigating around the UI, and reset when you close the
// window. Persisting to disk would be a second source of truth for no gain.

#include <stdint.h>
#include <string.h>
#include <map>
#include <string>

class Preferences {
    // Static so the "NVS" outlives any one begin()/end() pair, like the real one.
    static std::map<std::string, std::string>& store() {
        static std::map<std::string, std::string> s;
        return s;
    }
    std::string _ns;

public:
    bool begin(const char* name, bool = false) { _ns = name; return true; }
    void end() {}

    bool    getBool(const char* k, bool d)       { return get(k, d); }
    uint8_t getUChar(const char* k, uint8_t d)   { return get(k, d); }
    float   getFloat(const char* k, float d)     { return get(k, d); }

    void putBool(const char* k, bool v)     { put(k, v); }
    void putUChar(const char* k, uint8_t v) { put(k, v); }
    void putFloat(const char* k, float v)   { put(k, v); }

    // Returns bytes copied, 0 if absent — the contract settings.cpp checks.
    size_t getString(const char* k, char* out, size_t max) {
        auto it = store().find(key(k));
        if (it == store().end() || max == 0) return 0;
        size_t n = it->second.size();
        if (n > max - 1) n = max - 1;
        memcpy(out, it->second.data(), n);
        out[n] = 0;
        return n;
    }
    void putString(const char* k, const char* v) { store()[key(k)] = v ? v : ""; }

private:
    std::string key(const char* k) const { return _ns + "/" + k; }

    template <typename T> T get(const char* k, T d) {
        auto it = store().find(key(k));
        if (it == store().end() || it->second.size() != sizeof(T)) return d;
        T v;
        memcpy(&v, it->second.data(), sizeof(T));
        return v;
    }
    template <typename T> void put(const char* k, T v) {
        store()[key(k)] = std::string((const char*)&v, sizeof(T));
    }
};
