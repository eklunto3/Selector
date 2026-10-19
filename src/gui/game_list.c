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
    list->letter_on_dir = 0;
}

int gl_load(gl_list *list, const char *path, int format)
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

    /* Split each line in place at its tabs. */
    p = list->text;
    while (*p) {
        char *eol = strchr(p, '\n');
        char *field[5];
        int nf = 0;
        if (eol) *eol = 0;
        if (eol && eol > p && eol[-1] == '\r') eol[-1] = 0;
        field[nf++] = p;
        while (nf < 5) {
            char *tab = strchr(field[nf - 1], '\t');
            if (!tab) break;
            *tab = 0;
            field[nf++] = tab + 1;
        }
        if (*p) {
            gl_entry *e = &list->items[list->count];
            e->name = field[0];
            e->crc = e->dir = e->desc = "";
            e->size = 0;
            e->age = 0;
            if (format == GL_FMT_AMINET) {
                if (nf > 1) e->dir = field[1];
                if (nf > 2) e->size = strtoul(field[2], 0, 10);
                if (nf > 3) e->age = (unsigned short)strtoul(field[3], 0, 10);
                if (nf > 4) e->desc = field[4];
            } else {
                if (nf > 1) e->size = strtoul(field[1], 0, 10);
                if (nf > 2) e->crc = field[2];
            }
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
        int first = (unsigned char)((list->letter_on_dir && e->dir[0]) ? e->dir[0] : e->name[0]);
        if (letter == GL_LETTER_DIGITS) {
            if (first >= 'A' && first <= 'Z') continue;
            if (first >= 'a' && first <= 'z') continue;
        } else if (letter != GL_LETTER_ALL) {
            if (lower(first) != lower(letter)) continue;
        }
        if (!contains(e->name, needle) && !contains(e->desc, needle)) continue;
        out[n++] = e;
    }
    return n;
}
