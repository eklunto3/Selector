#ifndef GUI_GAME_LIST_H
#define GUI_GAME_LIST_H

/* Archive list of one pack, read from the "<name>\t<size>\t<crc>" text
 * files that whdfetch's DAT phase writes to "temp/Dat files/". */

typedef struct {
    const char *name;     /* archive file name, e.g. "Alien3_v2.2.lha" */
    unsigned long size;   /* bytes, 0 if unknown */
    const char *crc;      /* CRC-32 hex as in the DAT, "" if unknown */
} gl_entry;

typedef struct {
    gl_entry *items;
    long count;
    char *text;           /* file contents; names point into this */
} gl_list;

/* Loads a list file. Returns 0 on failure (list stays empty). */
int gl_load(gl_list *list, const char *path);
void gl_free(gl_list *list);

/* Letter filter values for gl_filter. */
#define GL_LETTER_ALL    0
#define GL_LETTER_DIGITS '#'

/* Copies pointers of matching entries to out (room for list->count) and
 * returns how many matched. search is a case-insensitive substring
 * ("" = all); letter is GL_LETTER_ALL, GL_LETTER_DIGITS or 'A'..'Z'. */
long gl_filter(const gl_list *list, const char *search, int letter, gl_entry **out);

#endif
