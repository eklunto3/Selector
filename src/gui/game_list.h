#ifndef GUI_GAME_LIST_H
#define GUI_GAME_LIST_H

/* Package list of one pack/category, read from tab-separated text files:
 *   Retroplay (whdfetch's DAT phase): name<TAB>size<TAB>crc
 *   Aminet (aminet_split_index):      name<TAB>dir<TAB>size<TAB>age<TAB>description */

#define GL_FMT_RETROPLAY 0
#define GL_FMT_AMINET    1

typedef struct {
    const char *name;     /* archive file name, e.g. "Alien3_v2.2.lha" */
    unsigned long size;   /* bytes, 0 if unknown */
    const char *crc;      /* CRC-32 hex (Retroplay), "" otherwise */
    const char *dir;      /* Aminet directory, e.g. "util/arc"; "" otherwise */
    const char *desc;     /* Aminet short description; "" otherwise */
    unsigned short age;   /* Aminet age in weeks (999 = 19+ years); 0 otherwise */
} gl_entry;

typedef struct {
    gl_entry *items;
    long count;
    char *text;           /* file contents; strings point into this */
    int letter_on_dir;    /* letter filter uses dir (Modland author), not name */
} gl_list;

/* Loads a list file. Returns 0 on failure (list stays empty). */
int gl_load(gl_list *list, const char *path, int format);
void gl_free(gl_list *list);

/* Letter filter values for gl_filter. */
#define GL_LETTER_ALL    0
#define GL_LETTER_DIGITS '#'

/* Copies pointers of matching entries to out (room for list->count) and
 * returns how many matched. search is a case-insensitive substring of the
 * name or description ("" = all); letter is GL_LETTER_ALL,
 * GL_LETTER_DIGITS or 'A'..'Z'. */
long gl_filter(const gl_list *list, const char *search, int letter, gl_entry **out);

#endif
