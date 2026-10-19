/* Archive list loading and filtering for the GUI. Portable C only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/game_list.h"

static int lower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

void gl_free(gl_list *list)
{
    free(list->items);
    free(list->text);
    list->items = 0;
    list->text = 0;
    list->count = 0;
}

int gl_load(gl_list *list, const char *path)
{
    FILE *f;
    long size, lines = 0, i;
    char *p;

    gl_free(list);
    f = fopen(path, "rb");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return 0; }

    list->text = (char *)malloc((size_t)size + 1);
    if (!list->text) { fclose(f); return 0; }
    if (fread(list->text, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        gl_free(list);
        return 0;
    }
    fclose(f);
    list->text[size] = 0;

    for (i = 0; i < size; i++)
        if (list->text[i] == '\n') lines++;
    lines++;
    list->items = (gl_entry *)malloc((size_t)lines * sizeof(gl_entry));
    if (!list->items) { gl_free(list); return 0; }

    /* Split in place: name<TAB>size<TAB>crc, or just name. */
    p = list->text;
    while (*p) {
        char *eol = strchr(p, '\n');
        char *tab;
        if (eol) *eol = 0;
        if (eol && eol > p && eol[-1] == '\r') eol[-1] = 0;
        tab = strchr(p, '\t');
        if (tab) *tab = 0;
        if (*p) {
            list->items[list->count].name = p;
            list->items[list->count].size = tab ? strtoul(tab + 1, 0, 10) : 0;
            list->count++;
        }
        if (!eol) break;
        p = eol + 1;
    }
    return 1;
}

/* Case-insensitive substring test; needle is already lower case. */
static int contains(const char *hay, const char *needle)
{
    size_t n = strlen(needle);
    if (n == 0) return 1;
    for (; *hay; hay++) {
        size_t i;
        for (i = 0; i < n && hay[i] && lower((unsigned char)hay[i]) == needle[i]; i++)
            ;
        if (i == n) return 1;
    }
    return 0;
}

long gl_filter(const gl_list *list, const char *search, int letter, gl_entry **out)
{
    char needle[128];
    long i, n = 0;
    size_t k;

    for (k = 0; search[k] && k < sizeof(needle) - 1; k++) needle[k] = (char)lower((unsigned char)search[k]);
    needle[k] = 0;

    for (i = 0; i < list->count; i++) {
        gl_entry *e = &list->items[i];
        int first = (unsigned char)e->name[0];
        if (letter == GL_LETTER_DIGITS) {
            if (first >= 'A' && first <= 'Z') continue;
            if (first >= 'a' && first <= 'z') continue;
        } else if (letter != GL_LETTER_ALL) {
            if (lower(first) != lower(letter)) continue;
        }
        if (!contains(e->name, needle)) continue;
        out[n++] = e;
    }
    return n;
}
