#ifndef GUI_ENGINE_H
#define GUI_ENGINE_H

/* whdfetch's download/extract machinery driven from the GUI, in-process.
 * Same defaults, whdfetch.ini overrides, directories and extraction as
 * the whdfetch command line tool. */

/* Loads defaults and whdfetch.ini and opens bsdsocket.library.
 * Returns 0 on failure; msg then holds a reason. */
int engine_init(char *msg, int msg_len);
void engine_shutdown(void);

/* Downloads the index page and the DAT zips of all five packs and
 * rewrites the "temp/Dat files/*.txt" lists. Returns 0 on failure. */
int engine_refresh_lists(char *msg, int msg_len);

/* Result of engine_install. */
#define ENGINE_INSTALLED 0
#define ENGINE_SKIPPED   2    /* already downloaded or extracted */
#define ENGINE_FAILED    (-1)
#define ENGINE_CANCELLED (-2)  /* Ctrl-C during the download */

/* Downloads and extracts one archive of pack 0..4 (Games, Games beta,
 * Demos, Demos beta, Magazines - the cycle gadget order). */
int engine_install(int pack, const char *archive, unsigned long size, const char *crc);

/* Where extracted titles go, for the status line. */
const char *engine_extract_target(void);

#endif
