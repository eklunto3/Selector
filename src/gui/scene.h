#ifndef GUI_SCENE_H
#define GUI_SCENE_H

/* Scene.org party archive over plain HTTP:
 * http://ftp.scene.org/pub/parties/<year>/<party>/<compo>/<file>.
 * Directory pages are lighttpd listings (name, date, size per row).
 * Amiga compos are the compo directories with "amiga" in their name. */

#define SCENE_BASE_URL "http://ftp.scene.org/pub/"
#define SCENE_FIRST_YEAR 1995

/* Called for every row of a listing ("../" is skipped). */
typedef int (*scene_entry_fn)(const char *name, int is_dir, unsigned long size, void *user);

/* Parses a saved listing page. Returns the number of rows, -1 on error. */
long scene_parse_listing(const char *html_path, scene_entry_fn fn, void *user);

/* "136.3K", "1.4M", "512" -> bytes ("-" = 0). */
unsigned long scene_parse_size(const char *s);

/* True for compo directories of Amiga entries. */
int scene_is_amiga_dir(const char *name);

/* True for files worth listing (not .diz/.nfo/.txt descriptions). */
int scene_is_entry_file(const char *name);

#endif
