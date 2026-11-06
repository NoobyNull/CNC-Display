#pragma once
#include <stddef.h>
#include <stdint.h>

// The controller's whole config.yaml, held in memory as "section/sub/key" ->
// value, filled from the $CD dump at every connect. Any screen asks for the
// key it wants by path; none of them needs its own query or its own parser.
//
//   cfg_get("axes/x/steps_per_mm")  ->  "15.953"
//
// The settings table ($S / $$) is a separate store (fnc_settings.h); this one
// is the YAML tree, which holds what $S does not: pins, per-axis numbers,
// macros, spindle and probe blocks.

void cfg_clear();

// One line of the dump. Lines that are not "key: value" / "key:" (comments,
// blanks, list items) are ignored. Returns true if it stored or opened a key.
bool cfg_feed_line(const char* line);

// Value for a path, or nullptr if the dump never carried it. Section headers
// ("axes") have an empty value.
const char* cfg_get(const char* path);
float       cfg_float(const char* path, float fallback);

size_t      cfg_count();
const char* cfg_path_at(size_t i);
const char* cfg_value_at(size_t i);

// Bumped when the contents change, so a page can tell when to redraw.
uint32_t    cfg_generation();
