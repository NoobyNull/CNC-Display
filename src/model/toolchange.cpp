#include "model/toolchange.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static int  s_tool = 0;
static char s_name[96];
static bool s_measured = false;

int         tc_tool()     { return s_tool; }
const char* tc_name()     { return s_name; }
bool        tc_measured() { return s_measured; }
void        tc_clear()    { s_tool = 0; s_name[0] = 0; s_measured = false; }

void tc_note_message(const char* a) {
    if (!a || !(a = strstr(a, "MSG,"))) return;
    a += 4;
    const char* p;
    if ((p = strstr(a, "Install tool #"))) {               // atc_manual, right before its M0
        const int t = atoi(p + 14);
        if (t <= 0) return;
        if (t != s_tool) s_name[0] = 0;                    // a name for another tool is stale
        s_tool = t;
        s_measured = true;
    } else if ((p = strstr(a, "Change to T")) || (p = strstr(a, "Load T"))) {  // the post
        p = strchr(p, 'T');
        const int t = atoi(p + 1);
        if (t <= 0) return;
        s_tool = t;
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
