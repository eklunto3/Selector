/* Aminet index handling. Portable C only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/aminet.h"
#include "gui/textsearch.h"

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

/* Splits one INDEX/RECENT line "name dir size age description".
 * Returns 0 for header and malformed lines. */
static int parse_line(char *line, char **name, char **dir, char **size, char **age, char **desc)
{
    char *p;
    size_t n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
    if (line[0] == '|' || line[0] == 0) return 0;     /* header */
    p = token(line, name);
    p = token(p, dir);
    p = token(p, size);
    p = token(p, age);
    while (*p == ' ' || *p == '\t') p++;
    *desc = p;
    return **name && **dir && **size && **age;
}

static void write_entry(FILE *f, const char *name, const char *dir, const char *size,
                        const char *age, const char *desc)
{
    fprintf(f, "%s\t%s\t%lu\t%ld\t%s\n", name, dir, aminet_parse_size(size),
            strtol(age, 0, 10), desc);
}

long aminet_convert_list(const char *in_path, const char *out_path)
{
    char line[512], tmp[300];
    FILE *in, *out;
    long count = 0;
    int ok;

    if (strlen(out_path) + 5 > sizeof(tmp)) return -1;
    sprintf(tmp, "%s.tmp", out_path);
    in = fopen(in_path, "r");
    if (!in) return -1;
    out = fopen(tmp, "w");
    if (!out) { fclose(in); return -1; }
    while (fgets(line, sizeof(line), in)) {
        char *name, *dir, *size, *age, *desc;
        if (!parse_line(line, &name, &dir, &size, &age, &desc)) continue;
        write_entry(out, name, dir, size, age, desc);
        count++;
    }
    fclose(in);
    ok = fclose(out) == 0;
    if (ok) {
        remove(out_path);
        ok = rename(tmp, out_path) == 0;
    }
    if (!ok) { remove(tmp); return -1; }
    return count;
}

const char *aminet_age_text(unsigned int weeks, char *buf)
{
    if (weeks == 0) strcpy(buf, "this week");
    else if (weeks == 1) strcpy(buf, "1 week");
    else if (weeks < 9) sprintf(buf, "%u weeks", weeks);
    else if (weeks < 104) sprintf(buf, "%u months", (weeks * 7 + 15) / 30);
    else if (weeks >= 999) strcpy(buf, "19+ years");
    else sprintf(buf, "%u years", weeks / 52);
    return buf;
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
        char *name, *dir, *size, *age, *desc;
        int cat;
        if (!parse_line(line, &name, &dir, &size, &age, &desc)) continue;
        cat = category_index(dir);
        if (cat < 0) continue;
        write_entry(out[cat], name, dir, size, age, desc);
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

long aminet_search(const char *dir, const char *query, const char *out_path,
                   long max_hits, int *truncated, int *files_read)
{
    static char paths[AMINET_NUM_CATEGORIES][300];
    const char *list[AMINET_NUM_CATEGORIES];
    int i;
    for (i = 0; i < AMINET_NUM_CATEGORIES; i++) {
        sprintf(paths[i], "%.250s/%s.txt", dir, aminet_categories[i]);
        list[i] = paths[i];
    }
    return ts_search_files(list, AMINET_NUM_CATEGORIES, query, out_path, max_hits,
                           truncated, files_read);
}
