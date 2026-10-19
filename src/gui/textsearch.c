/* Word search over list files. Portable C only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/textsearch.h"

#define IN_BUF  (32 * 1024)    /* vbcc's default stdio buffer is 1 KB */
#define OUT_BUF (8 * 1024)

static unsigned char lower_tab[256];
static int lower_ready;

static void init_lower(void)
{
    int i;
    for (i = 0; i < 256; i++) lower_tab[i] = (unsigned char)((i >= 'A' && i <= 'Z') ? i + 32 : i);
    lower_ready = 1;
}

void ts_prepare(const char *query, ts_query *q)
{
    size_t i;
    char *p;
    if (!lower_ready) init_lower();
    for (i = 0; query && query[i] && i < sizeof(q->text) - 1; i++)
        q->text[i] = (char)lower_tab[(unsigned char)query[i]];
    q->text[i] = 0;
    q->count = 0;
    p = q->text;
    while (*p && q->count < 8) {
        while (*p == ' ') p++;
        if (!*p) break;
        q->word[q->count] = p;
        while (*p && *p != ' ') p++;
        q->len[q->count] = (size_t)(p - q->word[q->count]);
        q->count++;
    }
}

static int has_word(const char *s, const char *end, const char *w, size_t n)
{
    unsigned char first = (unsigned char)w[0];
    if ((size_t)(end - s) < n) return 0;
    end -= n;
    for (; s <= end; s++) {
        size_t i;
        if (lower_tab[(unsigned char)*s] != first) continue;
        for (i = 1; i < n && lower_tab[(unsigned char)s[i]] == (unsigned char)w[i]; i++)
            ;
        if (i == n) return 1;
    }
    return 0;
}

int ts_matches(const char *line, const char *end, const ts_query *q)
{
    int i;
    for (i = 0; i < q->count; i++)
        if (!has_word(line, end, q->word[i], q->len[i])) return 0;
    return 1;
}

long ts_search_files(const char *const *paths, int count, const char *query,
                     const char *out_path, long max_hits, int *truncated, int *files_read)
{
    ts_query q;
    char *line, *inbuf;
    FILE *out;
    long hits = 0;
    int i;

    *truncated = 0;
    if (files_read) *files_read = 0;
    ts_prepare(query, &q);
    line = (char *)malloc(1024);
    inbuf = (char *)malloc(IN_BUF);
    out = fopen(out_path, "w");
    if (!line || !out) {
        free(line);
        free(inbuf);
        if (out) fclose(out);
        return -1;
    }
    setvbuf(out, NULL, _IOFBF, OUT_BUF);

    for (i = 0; i < count && !*truncated; i++) {
        FILE *in = fopen(paths[i], "r");
        if (!in) continue;
        if (inbuf) setvbuf(in, inbuf, _IOFBF, IN_BUF);
        if (files_read) (*files_read)++;
        while (fgets(line, 1024, in)) {
            if (!ts_matches(line, line + strlen(line), &q)) continue;
            fputs(line, out);
            if (++hits >= max_hits) { *truncated = 1; break; }
        }
        fclose(in);
    }
    free(inbuf);
    free(line);
    if (fclose(out) != 0) return -1;
    return hits;
}
