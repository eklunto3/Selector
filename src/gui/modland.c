/* Modland index handling. Portable C only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/modland.h"
#include "gui/inflate.h"

/* Amiga formats, by Modland directory name (checked 2026-10-19). */
typedef struct {
    const char *label;
    const char *dirs[6];
    int split;                 /* stored per author initial */
} ml_category;

static const ml_category categories[] = {
    { "Protracker",        { "Protracker", NULL }, 1 },
    { "Soundtracker",      { "Soundtracker", "Soundtracker 2.6", "Soundtracker Pro II", NULL }, 0 },
    { "Startrekker",       { "Startrekker AM", "Startrekker FLT8", NULL }, 0 },
    { "OctaMED",           { "OctaMED MMD0", "OctaMED MMD1", "OctaMED MMD2", "OctaMED MMD3", "OctaMED MMDC", NULL }, 0 },
    { "AHX",               { "AHX", NULL }, 0 },
    { "HivelyTracker",     { "HivelyTracker", NULL }, 0 },
    { "Future Composer",   { "Future Composer 1.3", "Future Composer 1.4", "Future Composer BSI", NULL }, 0 },
    { "TFMX",              { "TFMX", NULL }, 0 },
    { "DigiBooster",       { "Digibooster", "Digibooster Pro", NULL }, 0 },
    { "Musicline Editor",  { "Musicline Editor", NULL }, 0 },
    { "Oktalyzer",         { "Oktalyzer", NULL }, 0 },
    { "Art Of Noise",      { "Art Of Noise", NULL }, 0 },
    { "BP SoundMon",       { "BP SoundMon 1", "BP SoundMon 2", "BP SoundMon 3", NULL }, 0 },
    { "Sonic Arranger",    { "Sonic Arranger", NULL }, 0 },
    { "SidMon",            { "SidMon 1", "SidMon 2", NULL }, 0 },
    { "Delta Music",       { "Delta Music", "Delta Music 2", NULL }, 0 },
    { "Digital Mugician",  { "Digital Mugician", "Digital Mugician 2", "Digital Mugician 3", NULL }, 0 },
    { "JamCracker",        { "JamCracker", NULL }, 0 },
    { "Symphonie",         { "Symphonie", NULL }, 0 },
    { "DeliTracker Custom",{ "Delitracker Custom", NULL }, 0 },
};

#define NCAT ((int)(sizeof(categories) / sizeof(categories[0])))
#define NLETTERS 27            /* A..Z and '#' */

static const char *labels[NCAT + 1];

int modland_category_count(void) { return NCAT; }

const char **modland_labels(void)
{
    int i;
    for (i = 0; i < NCAT; i++) labels[i] = categories[i].label;
    labels[NCAT] = NULL;
    return labels;
}

int modland_category_split(int cat)
{
    return cat >= 0 && cat < NCAT && categories[cat].split;
}

static int letter_slot(int c)
{
    if (c >= 'a' && c <= 'z') c -= 32;
    return (c >= 'A' && c <= 'Z') ? c - 'A' : 26;
}

void modland_list_path(const char *dir, int cat, int letter, char *out)
{
    if (letter && modland_category_split(cat)) {
        int slot = letter_slot(letter);
        sprintf(out, "%s/%d_%c.txt", dir, cat, slot == 26 ? '#' : 'A' + slot);
    } else {
        sprintf(out, "%s/%d.txt", dir, cat);
    }
}

static int category_of(const char *format, size_t len)
{
    int i, j;
    for (i = 0; i < NCAT; i++)
        for (j = 0; categories[i].dirs[j]; j++)
            if (strlen(categories[i].dirs[j]) == len && strncmp(categories[i].dirs[j], format, len) == 0)
                return i;
    return -1;
}

/* ---- streaming split ---- */

typedef struct {
    FILE *out[NCAT][NLETTERS];   /* [cat][0] for unsplit categories */
    char line[1024];
    int len;
    int overflow;
    long kept;
    int failed;
} split_state;

static void handle_line(split_state *st, char *line)
{
    char *tab = strchr(line, '\t');
    char *path, *slash, *author, *name, *end;
    int cat, slot = 0;
    FILE *f;

    if (!tab) return;
    *tab = 0;
    path = tab + 1;
    slash = strchr(path, '/');
    if (!slash) return;
    cat = category_of(path, (size_t)(slash - path));
    if (cat < 0) return;

    name = strrchr(path, '/') + 1;
    author = slash + 1;
    end = strchr(author, '/');
    if (!*name) return;

    if (categories[cat].split) slot = letter_slot((unsigned char)*author);
    f = st->out[cat][slot];
    if (!f) return;
    /* name<TAB>author<TAB>size<TAB>0<TAB>path; files directly under the
     * format directory have no author. */
    fputs(name, f);
    fputc('\t', f);
    if (end && end != author) fwrite(author, 1, (size_t)(end - author), f);
    fprintf(f, "\t%s\t0\t%s\n", line, path);
    st->kept++;
}

static int sink(const unsigned char *data, unsigned n, void *user)
{
    split_state *st = (split_state *)user;
    unsigned i;
    for (i = 0; i < n; i++) {
        char c = (char)data[i];
        if (c == '\n') {
            if (!st->overflow) {
                if (st->len > 0 && st->line[st->len - 1] == '\r') st->len--;
                st->line[st->len] = 0;
                handle_line(st, st->line);
            }
            st->len = 0;
            st->overflow = 0;
        } else if (st->len < (int)sizeof(st->line) - 1) {
            st->line[st->len++] = c;
        } else {
            st->overflow = 1;      /* absurdly long line: skip it */
        }
    }
    return 1;
}

long modland_split_index(const char *zip_path, const char *out_dir)
{
    split_state *st;
    char tmp[310], final_path[300];
    int c, l, r;
    long kept;

    st = (split_state *)calloc(1, sizeof(split_state));
    if (!st) return -1;
    for (c = 0; c < NCAT; c++) {
        int n = categories[c].split ? NLETTERS : 1;
        for (l = 0; l < n; l++) {
            modland_list_path(out_dir, c, categories[c].split ? (l == 26 ? '#' : 'A' + l) : 0, final_path);
            sprintf(tmp, "%s.tmp", final_path);
            st->out[c][l] = fopen(tmp, "w");
            if (!st->out[c][l]) st->failed = 1;
        }
    }

    r = st->failed ? IF_ERR_IO : inflate_unzip_first_to_sink(zip_path, sink, st);

    for (c = 0; c < NCAT; c++) {
        int n = categories[c].split ? NLETTERS : 1;
        for (l = 0; l < n; l++) {
            if (!st->out[c][l]) continue;
            if (fclose(st->out[c][l]) != 0) r = IF_ERR_IO;
            modland_list_path(out_dir, c, categories[c].split ? (l == 26 ? '#' : 'A' + l) : 0, final_path);
            sprintf(tmp, "%s.tmp", final_path);
            if (r == IF_OK) {
                remove(final_path);
                if (rename(tmp, final_path) != 0) r = IF_ERR_IO;
            } else {
                remove(tmp);
            }
        }
    }
    kept = st->kept;
    free(st);
    return r == IF_OK ? kept : -1;
}

/* ---- search over all lists ---- */

static int lower_c(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

/* Case-insensitive substring; needle already lower case. */
static int has_word(const char *hay, const char *needle, size_t n)
{
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < n && hay[i] && lower_c((unsigned char)hay[i]) == needle[i]; i++)
            ;
        if (i == n) return 1;
    }
    return 0;
}

/* All space-separated words of query must occur in line. */
static int line_matches(const char *line, const char *query)
{
    const char *q = query;
    while (*q) {
        size_t n;
        while (*q == ' ') q++;
        n = 0;
        while (q[n] && q[n] != ' ') n++;
        if (n && !has_word(line, q, n)) return 0;
        q += n;
    }
    return 1;
}

/* Author initial of a list line "name<TAB>author<TAB>...". */
static int line_letter(const char *line)
{
    const char *tab = strchr(line, '\t');
    return tab ? letter_slot((unsigned char)tab[1]) : 26;
}

long modland_collect(const char *dir, int letter, const char *query, const char *out_path,
                     long max_hits, int *truncated, int *files_read)
{
    char lower_query[128], path[300], line[1024];
    FILE *out;
    long hits = 0;
    int c, l, want = letter ? letter_slot(letter) : -1;
    size_t i;

    *truncated = 0;
    if (files_read) *files_read = 0;
    for (i = 0; query && query[i] && i < sizeof(lower_query) - 1; i++)
        lower_query[i] = (char)lower_c((unsigned char)query[i]);
    lower_query[i] = 0;

    out = fopen(out_path, "w");
    if (!out) return -1;
    for (c = 0; c < NCAT && !*truncated; c++) {
        int n = categories[c].split ? NLETTERS : 1;
        for (l = 0; l < n && !*truncated; l++) {
            FILE *in;
            /* Split lists are already per letter: read only the right one. */
            if (categories[c].split && want >= 0 && l != want) continue;
            modland_list_path(dir, c, categories[c].split ? (l == 26 ? '#' : 'A' + l) : 0, path);
            in = fopen(path, "r");
            if (!in) continue;
            if (files_read) (*files_read)++;
            while (fgets(line, sizeof(line), in)) {
                if (want >= 0 && line_letter(line) != want) continue;
                if (lower_query[0] && !line_matches(line, lower_query)) continue;
                fputs(line, out);
                if (++hits >= max_hits) { *truncated = 1; break; }
            }
            fclose(in);
        }
    }
    if (fclose(out) != 0) return -1;
    return hits;
}

long modland_search(const char *dir, const char *query, const char *out_path,
                    long max_hits, int *truncated)
{
    return modland_collect(dir, 0, query, out_path, max_hits, truncated, NULL);
}

/* ---- names and URLs ---- */

int modland_url_encode(const char *path, char *out, int outlen)
{
    static const char hex[] = "0123456789ABCDEF";
    int n = 0;
    for (; *path; path++) {
        unsigned char c = (unsigned char)*path;
        int safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || strchr("-_.~/()!*',", c);
        if (safe) {
            if (n + 1 >= outlen) return 0;
            out[n++] = (char)c;
        } else {
            if (n + 3 >= outlen) return 0;
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
    return 1;
}

void modland_local_name(const char *name, char *out)
{
    char clean[256];
    const char *dot;
    size_t n = 0, ext_len;

    for (; *name && n < sizeof(clean) - 1; name++)
        clean[n++] = (*name == ':' || *name == '/' || *name == ' ' || (unsigned char)*name < 32) ? '_' : *name;
    clean[n] = 0;
    if (n <= 30) {
        strcpy(out, clean);
        return;
    }
    dot = strrchr(clean, '.');
    ext_len = dot ? strlen(dot) : 0;
    if (ext_len > 8) ext_len = 0;          /* not a real extension */
    memcpy(out, clean, 30 - ext_len);
    if (ext_len) memcpy(out + 30 - ext_len, clean + n - ext_len, ext_len);
    out[30] = 0;
}
