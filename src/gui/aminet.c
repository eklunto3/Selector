/* Aminet index handling. Portable C only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/aminet.h"

const char *aminet_categories[AMINET_NUM_CATEGORIES + 1] = {
    "biz", "comm", "demo", "dev", "disk", "docs", "driver", "game",
    "gfx", "mags", "misc", "mods", "mus", "pix", "text", "util", NULL
};

unsigned long aminet_parse_size(const char *s)
{
    unsigned long whole = 0, frac = 0, scale = 1, unit = 1;
    while (*s >= '0' && *s <= '9') whole = whole * 10 + (unsigned long)(*s++ - '0');
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            if (scale < 1000) { frac = frac * 10 + (unsigned long)(*s - '0'); scale *= 10; }
            s++;
        }
    }
    if (*s == 'K' || *s == 'k') unit = 1024UL;
    else if (*s == 'M' || *s == 'm') unit = 1024UL * 1024UL;
    return whole * unit + frac * unit / scale;
}

static int category_index(const char *dir)
{
    int i;
    for (i = 0; i < AMINET_NUM_CATEGORIES; i++) {
        size_t n = strlen(aminet_categories[i]);
        if (strncmp(dir, aminet_categories[i], n) == 0 && (dir[n] == '/' || dir[n] == 0))
            return i;
    }
    return -1;
}

/* Next whitespace-separated token; returns pointer after it. */
static char *token(char *p, char **start)
{
    while (*p == ' ' || *p == '\t') p++;
    *start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    if (*p) *p++ = 0;
    return p;
}

long aminet_split_index(const char *index_path, const char *out_dir)
{
    FILE *in, *out[AMINET_NUM_CATEGORIES];
    char line[512], path[300];
    long count = 0;
    int i, ok = 1;

    in = fopen(index_path, "r");
    if (!in) return -1;
    for (i = 0; i < AMINET_NUM_CATEGORIES; i++) {
        sprintf(path, "%s/%s.txt.tmp", out_dir, aminet_categories[i]);
        out[i] = fopen(path, "w");
        if (!out[i]) ok = 0;
    }

    while (ok && fgets(line, sizeof(line), in)) {
        char *name, *dir, *size, *age, *desc, *p;
        size_t n = strlen(line);
        int cat;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (line[0] == '|' || line[0] == 0) continue;     /* header */

        p = token(line, &name);
        p = token(p, &dir);
        p = token(p, &size);
        p = token(p, &age);
        while (*p == ' ' || *p == '\t') p++;
        desc = p;
        if (!*name || !*dir || !*size || !age) continue;
        cat = category_index(dir);
        if (cat < 0) continue;
        fprintf(out[cat], "%s\t%s\t%lu\t%s\n", name, dir, aminet_parse_size(size), desc);
        count++;
    }
    fclose(in);

    for (i = 0; i < AMINET_NUM_CATEGORIES; i++) {
        char final_path[300];
        if (!out[i]) continue;
        if (fclose(out[i]) != 0) ok = 0;
        sprintf(path, "%s/%s.txt.tmp", out_dir, aminet_categories[i]);
        sprintf(final_path, "%s/%s.txt", out_dir, aminet_categories[i]);
        if (ok) {
            remove(final_path);
            if (rename(path, final_path) != 0) ok = 0;
        } else {
            remove(path);
        }
    }
    return ok ? count : -1;
}
