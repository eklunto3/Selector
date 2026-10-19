#ifndef GUI_MODLAND_H
#define GUI_MODLAND_H

/* Modland music archive: http://ftp.modland.com/allmods.zip holds one
 * text file, "size<TAB>Format/Author/.../file" per module (516,187 lines,
 * 30 MB on 2026-10-19). Module URL: MODLAND_BASE_URL + path. Only formats
 * that play on an Amiga are kept (see modland.c). */

#define MODLAND_INDEX_URL "http://ftp.modland.com/allmods.zip"
#define MODLAND_BASE_URL  "http://ftp.modland.com/pub/modules/"

int modland_category_count(void);
const char **modland_labels(void);             /* NULL-terminated, for a cycle */

/* Categories with many modules are stored per author initial; the GUI
 * then needs a letter ('A'..'Z' or '#') to load a list. */
int modland_category_split(int cat);

/* List file for a category ("temp/Modland/<n>.txt", or
 * "temp/Modland/<n>_<L>.txt" for split categories; letter 0 = whole). */
void modland_list_path(const char *dir, int cat, int letter, char *out);

/* Streams the zipped index and writes the list files into out_dir with
 * lines "name<TAB>author<TAB>size<TAB>0<TAB>path" (GL_FMT_AMINET layout).
 * Returns the number of modules kept, or -1. */
long modland_split_index(const char *zip_path, const char *out_dir);

/* Searches every list file in dir (all formats, all author initials)
 * for query (case-insensitive substring of module name, author or path;
 * words separated by spaces must all match) and writes up to max_hits
 * matching lines to out_path in the same format. Returns the number of
 * hits, -1 on a file error. *truncated is set if max_hits was reached. */
long modland_search(const char *dir, const char *query, const char *out_path,
                    long max_hits, int *truncated);

/* Percent-encodes a module path for the URL ('/' kept). 0 if too long. */
int modland_url_encode(const char *path, char *out, int outlen);

/* Name usable on an OS 3.1 FFS volume and on a command line: ":", "/" and
 * spaces become "_", at most 30 chars
 * (the extension is kept when shortening). out needs 31 bytes. */
void modland_local_name(const char *name, char *out);

#endif
