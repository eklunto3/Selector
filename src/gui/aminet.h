#ifndef GUI_AMINET_H
#define GUI_AMINET_H

/* Aminet source: http://aminet.net/INDEX.gz (one line per package:
 * "name  dir  size  age  description"), split into one list file per
 * top-level category. Package URL: http://aminet.net/<dir>/<name>. */

#define AMINET_BASE_URL  "http://aminet.net/"
#define AMINET_INDEX_URL "http://aminet.net/INDEX.gz"

/* Top-level categories (fixed on Aminet; checked 2026-10-19). */
#define AMINET_NUM_CATEGORIES 16
extern const char *aminet_categories[AMINET_NUM_CATEGORIES + 1];  /* NULL-terminated */

/* "671K" / "9.5M" -> bytes. */
unsigned long aminet_parse_size(const char *s);

/* Splits an uncompressed INDEX into out_dir/<category>.txt with lines
 * "name<TAB>dir<TAB>bytes<TAB>description". Returns the number of
 * packages, or -1 on a file error. */
long aminet_split_index(const char *index_path, const char *out_dir);

#endif
