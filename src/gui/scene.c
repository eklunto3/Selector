/* Scene.org listing parser. Portable C only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/scene.h"

static int lower(int c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }

static int contains_ci(const char *s, const char *w)
{
    size_t n = strlen(w);
    for (; *s; s++) {
        size_t i;
        for (i = 0; i < n && s[i] && lower((unsigned char)s[i]) == w[i]; i++)
            ;
        if (i == n) return 1;
    }
    return 0;
}

static int ends_with_ci(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix), i;
    if (n < m) return 0;
    for (i = 0; i < m; i++)
        if (lower((unsigned char)s[n - m + i]) != suffix[i]) return 0;
    return 1;
}

int scene_is_amiga_dir(const char *name) { return contains_ci(name, "amiga"); }

int scene_is_entry_file(const char *name)
{
    return !ends_with_ci(name, ".diz") && !ends_with_ci(name, ".nfo") &&
           !ends_with_ci(name, ".txt") && !ends_with_ci(name, ".htm") &&
           !ends_with_ci(name, ".html") && name[0] != '.';
}

unsigned long scene_parse_size(const char *s)
{
    unsigned long whole = 0, frac = 0, scale = 1, unit = 1;
    while (*s == ' ') s++;
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
    else if (*s == 'G' || *s == 'g') unit = 1024UL * 1024UL * 1024UL;
    return whole * unit + frac * unit / scale;
}

/* Decodes %XX and &amp; in place. */
static void decode(char *s)
{
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == '%' && r[1] && r[2]) {
            char hex[3] = { r[1], r[2], 0 };
            char *end;
            long v = strtol(hex, &end, 16);
            if (*end == 0) { *w++ = (char)v; r += 3; continue; }
        }
        if (strncmp(r, "&amp;", 5) == 0) { *w++ = '&'; r += 5; continue; }
        *w++ = *r++;
    }
    *w = 0;
}

long scene_parse_listing(const char *html_path, scene_entry_fn fn, void *user)
{
    FILE *f;
    long size, rows = 0;
    char *html, *p;

    f = fopen(html_path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) { fclose(f); return -1; }
    html = (char *)malloc((size_t)size + 1);
    if (!html) { fclose(f); return -1; }
    if (fread(html, 1, (size_t)size, f) != (size_t)size) { fclose(f); free(html); return -1; }
    fclose(f);
    html[size] = 0;

    /* Each row: <tr ...><td class="n"><a href="NAME">..</a>..
     *           <td class="m">DATE</td><td class="s">SIZE</td> */
    p = html;
    while ((p = strstr(p, "<td class=\"n\"><a href=\"")) != NULL) {
        char name[256], sz[32];
        char *q = p + 23, *end = strchr(q, '"'), *row_end, *s;
        size_t n;
        int is_dir;
        if (!end) break;
        n = (size_t)(end - q);
        p = end;
        if (n == 0 || n >= sizeof(name)) continue;
        memcpy(name, q, n);
        name[n] = 0;
        if (strcmp(name, "../") == 0 || strchr(name, '?')) continue;
        is_dir = name[n - 1] == '/';
        if (is_dir) name[n - 1] = 0;
        if (strchr(name, '/')) continue;      /* only this directory */
        decode(name);

        sz[0] = 0;
        row_end = strstr(p, "</tr>");
        s = strstr(p, "<td class=\"s\">");
        if (s && (!row_end || s < row_end)) {
            size_t k = 0;
            s += 14;
            while (*s && *s != '<' && *s != '&' && k < sizeof(sz) - 1) sz[k++] = *s++;
            sz[k] = 0;
        }
        rows++;
        if (fn(name, is_dir, is_dir ? 0 : scene_parse_size(sz), user)) break;
    }
    free(html);
    return rows;
}
