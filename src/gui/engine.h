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

/* Aminet: downloads INDEX.gz, unpacks it and writes the category lists
 * to temp/Aminet/<category>.txt. Returns 0 on failure. */
int engine_refresh_aminet(char *msg, int msg_len);

/* Downloads aminet.net/<dir>/<name> and extracts it with c:lha / c:unlzx
 * into <Aminet target>/<name without extension>/. ENGINE_* result. */
int engine_install_aminet(const char *dir, const char *name);
const char *engine_aminet_target(void);

/* Modland: downloads allmods.zip and writes the Amiga-format lists to
 * temp/Modland/ (see modland.h). Returns 0 on failure. */
int engine_refresh_modland(char *msg, int msg_len);
const char *engine_modland_dir(void);

/* Downloads one module (path as in the index) to
 * <Music target>/<author>/<name>. ENGINE_* result. */
int engine_install_modland(const char *path, const char *author, const char *name);
const char *engine_modland_target(void);

/* Where extracted titles go, for the status line. */
const char *engine_extract_target(void);

/* Settings shown in the Settings window; stored as whdfetch.ini keys
 * (extract_path, delete_archives_after_extract, skip_aga, skip_cd,
 * skip_ntsc, skip_non_english) so whdfetch and WHDSelector agree. */
typedef struct {
    char install_dir[256];   /* "" = whdfetch default (GameFiles/...) */
    int delete_archives;
    int skip_aga, skip_cd, skip_ntsc, skip_non_english;
    char aminet_dir[256];    /* "" = Aminet/ in the program directory */
    char modland_dir[256];   /* "" = Music/ in the program directory */
} engine_settings;

void engine_get_settings(engine_settings *s);

/* Applies the settings at once and writes them to PROGDIR:whdfetch.ini.
 * Returns 0 if the file could not be written (msg says why). */
int engine_save_settings(const engine_settings *s, char *msg, int msg_len);

#endif
