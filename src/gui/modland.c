/* Modland index handling. Portable C only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/modland.h"
#include "gui/inflate.h"
#include "gui/textsearch.h"

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

#define OUT_BUF (8 * 1024)     /* per output file; vbcc's default is 1 KB */
#define IN_BUF  (32 * 1024)    /* for reading lists */

typedef struct {
    FILE *out[NCAT][NLETTERS];   /* [cat][0] for unsplit categories */
    FILE *all[NLETTERS];         /* every format, by author initial */
    char line[1024];
    char rec[1200];
    int len;
    int overflow;
    long kept;
    int failed;
} split_state;

void modland_all_path(const char *dir, int letter, char *out)
{
    int slot = letter_slot(letter);
    sprintf(out, "%s/all_%c.txt", dir, slot == 26 ? '#' : 'A' + slot);
}

static void handle_line(split_state *st, char *line)
{
    char *tab = strchr(line, '\t');
    char *path, *slash, *author, *name, *end;
    int cat, slot = 0, aslot, n;
    size_t alen;
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
    /* Two-file formats: the sample file is fetched together with its song
     * file (see modland_companion), so it is not listed on its own. */
    if (strncmp(name, "smpl.", 5) == 0) return;
    /* Files directly under the format directory have no author. */
    alen = (end && end != author) ? (size_t)(end - author) : 0;
    if (alen > 200) alen = 200;
    aslot = alen ? letter_slot((unsigned char)*author) : 26;

    /* name<TAB>author<TAB>size<TAB>0<TAB>path */
    n = sprintf(st->rec, "%.300s\t%.*s\t%.20s\t0\t%.400s\n", name, (int)alen, author, line, path);

    if (categories[cat].split) slot = aslot;
    f = st->out[cat][slot];
    if (f) fwrite(st->rec, 1, (size_t)n, f);
    if (st->all[aslot]) fwrite(st->rec, 1, (size_t)n, st->all[aslot]);
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
            else setvbuf(st->out[c][l], NULL, _IOFBF, OUT_BUF);
        }
    }
    for (l = 0; l < NLETTERS; l++) {
        modland_all_path(out_dir, l == 26 ? '#' : 'A' + l, final_path);
        sprintf(tmp, "%s.tmp", final_path);
        st->all[l] = fopen(tmp, "w");
        if (!st->all[l]) st->failed = 1;
        else setvbuf(st->all[l], NULL, _IOFBF, OUT_BUF);
    }

    r = st->failed ? IF_ERR_IO : inflate_unzip_first_to_sink(zip_path, sink, st);

    for (l = 0; l < NLETTERS; l++) {
        if (!st->all[l]) continue;
        if (fclose(st->all[l]) != 0) r = IF_ERR_IO;
    }
    for (l = 0; l < NLETTERS; l++) {
        modland_all_path(out_dir, l == 26 ? '#' : 'A' + l, final_path);
        sprintf(tmp, "%s.tmp", final_path);
        if (r == IF_OK) {
            remove(final_path);
            if (rename(tmp, final_path) != 0) r = IF_ERR_IO;
        } else {
            remove(tmp);
        }
    }

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

/* Author initial of a list line "name<TAB>author<TAB>...". */
static int line_letter(const char *line)
{
    const char *tab = strchr(line, '\t');
    return tab ? letter_slot((unsigned char)tab[1]) : 26;
}

/* Optional in-memory copy of all lists (all_*.txt), for fast searches. */
static char *cache;
static long cache_len;

void modland_cache_free(void)
{
    free(cache);
    cache = NULL;
    cache_len = 0;
}

int modland_cache_loaded(void) { return cache != NULL; }

long modland_cache_load(const char *dir)
{
    char path[300];
    long total = 0, pos = 0;
    int l;

    modland_cache_free();
    for (l = 0; l < NLETTERS; l++) {
        FILE *f;
        modland_all_path(dir, l == 26 ? '#' : 'A' + l, path);
        f = fopen(path, "rb");
        if (!f) return -1;
        fseek(f, 0, SEEK_END);
        total += ftell(f);
        fclose(f);
    }
    cache = (char *)malloc((size_t)total + 1);
    if (!cache) return -1;
    for (l = 0; l < NLETTERS; l++) {
        FILE *f;
        size_t got;
        modland_all_path(dir, l == 26 ? '#' : 'A' + l, path);
        f = fopen(path, "rb");
        if (!f) { modland_cache_free(); return -1; }
        got = fread(cache + pos, 1, (size_t)(total - pos), f);
        fclose(f);
        pos += (long)got;
    }
    cache[pos] = 0;
    cache_len = pos;
    return cache_len;
}

long modland_collect(const char *dir, int letter, const char *query, const char *out_path,
                     long max_hits, int *truncated, int *files_read)
{
    char path[300], *line, *inbuf = NULL;
    ts_query q;
    FILE *out;
    long hits = 0;
    int l, want = letter ? letter_slot(letter) : -1;

    *truncated = 0;
    if (files_read) *files_read = 0;
    ts_prepare(query, &q);
    line = (char *)malloc(1024);
    if (!line) return -1;

    out = fopen(out_path, "w");
    if (!out) { free(line); return -1; }
    setvbuf(out, NULL, _IOFBF, OUT_BUF);

    if (cache && want < 0) {
        /* Whole catalogue in memory: no disk access at all. */
        const char *s = cache, *end = cache + cache_len;
        if (files_read) *files_read = NLETTERS;
        while (s < end && !*truncated) {
            const char *nl = memchr(s, '\n', (size_t)(end - s));
            const char *e = nl ? nl : end;
            if (ts_matches(s, e, &q)) {
                fwrite(s, 1, (size_t)(e - s), out);
                fputc('\n', out);
                if (++hits >= max_hits) *truncated = 1;
            }
            s = e + 1;
        }
    } else {
        inbuf = (char *)malloc(IN_BUF);
        for (l = 0; l < NLETTERS && !*truncated; l++) {
            FILE *in;
            if (want >= 0 && l != want) continue;
            modland_all_path(dir, l == 26 ? '#' : 'A' + l, path);
            in = fopen(path, "r");
            if (!in) continue;
            if (inbuf) setvbuf(in, inbuf, _IOFBF, IN_BUF);
            if (files_read) (*files_read)++;
            while (fgets(line, 1024, in)) {
                size_t n = strlen(line);
                if (want >= 0 && line_letter(line) != want) continue;
                if (q.count && !ts_matches(line, line + n, &q)) continue;
                fputs(line, out);
                if (++hits >= max_hits) { *truncated = 1; break; }
            }
            fclose(in);
        }
        free(inbuf);
    }
    free(line);
    if (fclose(out) != 0) return -1;
    return hits;
}

long modland_search(const char *dir, const char *query, const char *out_path,
                    long max_hits, int *truncated)
{
    return modland_collect(dir, 0, query, out_path, max_hits, truncated, NULL);
}

/* ---- two-file modules ---- */

int modland_companion(const char *path, char *out, int outlen)
{
    const char *name = strrchr(path, '/');
    size_t dir_len;
    name = name ? name + 1 : path;
    /* TFMX: mdat.<song> needs smpl.<song> from the same directory. */
    if (strncmp(name, "mdat.", 5) != 0) return 0;
    dir_len = (size_t)(name - path);
    if ((int)(strlen(path) + 1) > outlen) return 0;
    memcpy(out, path, dir_len);
    strcpy(out + dir_len, "smpl.");
    strcat(out, name + 5);
    return 1;
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
