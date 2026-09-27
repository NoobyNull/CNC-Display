#pragma once
#include <stdint.h>
#include <stddef.h>

// What a G-code file will do, gathered in one streaming pass as the file is
// pulled off the controller. Replaces drawing the toolpath: a flat line plot
// on an S3 never looked like the part, while these numbers answer the
// questions asked before pressing RUN -- how big, how deep, which tools, how
// long. All lengths in mm, whatever units the file uses.

static const int GSTATS_TOOLS    = 8;
static const int GSTATS_NOTES    = 32;   // VCarve alone writes ~20
static const int GSTATS_NOTE_LEN = 64;

struct gtool_t {
    int  num;
    char desc[40];      // the comment that named it, if any
};

struct gstats_t {
    uint32_t lines;          // every line in the file
    uint32_t moves;          // lines that moved the machine
    bool     have_cut;       // any feed (G1/G2/G3) move at all
    float    min_x, max_x, min_y, max_y, min_z, max_z;   // cutting moves only
    float    top_z;          // highest Z reached by anything: the clearance
    float    cut_mm, rapid_mm;
    float    cut_sec;        // cut_mm at the programmed feeds; rapids excluded
    float    feed_min, feed_max;
    float    spindle_max;
    bool     inches;         // file is in G20
    int      tool_changes;   // M6 count
    int      n_tools;
    gtool_t  tools[GSTATS_TOOLS];
    // Comment lines before the first move: where VCarve, Fusion and friends
    // write material size, XY/Z origin, toolpath and tool lists.
    int      n_notes;
    char     notes[GSTATS_NOTES][GSTATS_NOTE_LEN];
};

void            gstats_reset();
void            gstats_feed_line(const char* line);   // safe with comments, blanks, garbage
const gstats_t& gstats();
