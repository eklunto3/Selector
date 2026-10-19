#ifndef GUI_TEXTSEARCH_H
#define GUI_TEXTSEARCH_H

#include <stddef.h>

/* Word search shared by the Aminet and Modland lists: every word of the
 * query must occur somewhere in the line (case-insensitive, any order). */

typedef struct {
    char text[128];
    const char *word[8];
    size_t len[8];
    int count;
} ts_query;

void ts_prepare(const char *query, ts_query *q);

/* line .. end (end exclusive). */
int ts_matches(const char *line, const char *end, const ts_query *q);

/* Reads the files with 32 KB buffers and writes matching lines to
 * out_path, at most max_hits. Missing files are skipped; *files_read
 * (may be NULL) counts the ones found. Returns hits, -1 on error. */
long ts_search_files(const char *const *paths, int count, const char *query,
                     const char *out_path, long max_hits, int *truncated, int *files_read);

#endif
