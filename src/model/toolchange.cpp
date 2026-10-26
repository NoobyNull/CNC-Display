#include "model/toolchange.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool s_pending  = false;
static int  s_tool     = 0;
static bool s_remove   = false;
static char s_name[96];
static bool s_measured = false;

bool        tc_pending()  { return s_pending; }
int         tc_tool()     { return s_tool; }
bool        tc_remove()   { return s_remove; }
const char* tc_name()     { return s_name; }
bool        tc_measured() { return s_measured; }
void        tc_clear()    { s_pending = false; s_tool = 0; s_remove = false; s_name[0] = 0; s_measured = false; }

static void request(int t, bool remove, bool measured) {
    if (t != s_tool || remove) s_name[0] = 0;          // a name for another tool is stale
    s_pending  = true;
    s_tool     = t;
    s_remove   = remove;
    s_measured = measured;
}

void tc_note_message(const char* a) {
    if (!a) return;
    const char* p;
    // The operator steps. "#" from atc_manual; "T" from openatc_m6.nc, which
    // cannot print a literal '#' (FluidNC's PRINT reads it as a parameter).
    if ((p = strstr(a, "Install tool #")) || (p = strstr(a, "Install tool T"))) {
        request(atoi(p + 14), false, true);
    } else if ((p = strstr(a, "Remove tool T"))) {
        request(atoi(p + 13), true, false);
    } else if (strstr(a, "Remove the tool in the spindle")) {
        request(-1, true, false);
    } else if (const char* m = strstr(a, "MSG,")) {                     // the post
        if (!(p = strstr(m, "Change to T")) && !(p = strstr(m, "Load T"))) return;
        p = strchr(p, 'T');
        const int t = atoi(p + 1);
        if (t <= 0) return;
        s_pending  = true;
        s_tool     = t;
        s_remove   = false;
        s_measured = false;
        // "T2: 2 HOG Roughing ... - re-zero Z, then Resume" -> the name part
        const char* n = strchr(p, ':');
        n = n ? n + 1 : "";
        while (*n == ' ') n++;
        snprintf(s_name, sizeof(s_name), "%s", n);
        char* cut = strstr(s_name, " - ");
        if (cut) *cut = 0;
        for (char* e = s_name + strlen(s_name); e > s_name && (e[-1] == ' ' || e[-1] == ']'); ) *--e = 0;
    }
}
