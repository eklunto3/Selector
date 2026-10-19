/* whdfetch engine for the GUI: the same steps main.c runs, as functions.
 * Console output of the whdfetch modules still goes to stdout (the Shell
 * when started from one); the GUI shows its own status. */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "main.h"
#include "config/app_defaults.h"
#include "utilities.h"
#include "ini_parser.h"
#include "lifecycle/lifecycle.h"
#include "phases/dat_phase.h"
#include "phases/download_phase.h"
#include "phases/html_phase.h"
#include "report/report.h"
#include "download/amiga_download.h"
#include "extract/extract.h"
#include "log/log.h"
#include "gui/engine.h"

#define NUM_PACKS 5

static struct whdload_pack_def packs[NUM_PACKS];
static struct download_option options;
static BOOL download_lib_ready = FALSE;
static int initialised = 0;      /* network ready */
static int config_loaded = 0;    /* defaults + whdfetch.ini loaded */

/* GUI cycle order -> whdfetch pack index (see app_constants.c). */
static int pack_index(int gui_pack)
{
    switch (gui_pack) {
    case 0: return GAMES;
    case 1: return GAMES_BETA;
    case 2: return DEMOS;
    case 3: return DEMOS_BETA;
    default: return MAGAZINES;
    }
}

static int lower_ascii(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + 32 : c;
}

static int strnicmp_ascii(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int d = lower_ascii((unsigned char)a[i]) - lower_ascii((unsigned char)b[i]);
        if (d || !a[i]) return d;
    }
    return 0;
}

static void set_msg(char *msg, int len, const char *text)
{
    if (msg && len > 0) {
        strncpy(msg, text, (size_t)len - 1);
        msg[len - 1] = 0;
    }
}

int engine_init(char *msg, int msg_len)
{
    int i;

    /* Configuration once; the network part is retried on every call until
     * it works, so WHDSelector can be started before the TCP/IP stack. */
    if (!config_loaded) {
        setup_app_defaults(packs, &options);
        report_init();
        ini_parser_load_overrides(packs, &options);
        ensure_time_zone_set();
        start_time = time(NULL);

        /* The GUI decides per archive what to fetch; keep all packs active
         * so refresh processes every DAT. */
        for (i = 0; i < NUM_PACKS; i++) packs[i].user_requested_download = 1;
        config_loaded = 1;
    }
    if (initialised) return 1;

    if (!ad_init_download_lib_taglist(
            ADTAG_Verbose, FALSE,
            ADTAG_Timeout, options.timeout_seconds,
            ADTAG_BufferSize, 16384,
            ADTAG_UserAgent, "whdfetch/1.0",
            ADTAG_MaxRetries, 2,
            TAG_DONE)) {
        set_msg(msg, msg_len,
                "No network: cannot open bsdsocket.library. Put Miami online, then press Refresh list.");
        return 0;
    }
    download_lib_ready = TRUE;
    initialised = 1;
    return 1;
}

void engine_shutdown(void)
{
    if (!config_loaded) return;
    lifecycle_do_shutdown(&download_lib_ready);
    initialised = 0;
    config_loaded = 0;
}

int engine_refresh_lists(char *msg, int msg_len)
{
    char index_path[256];
    int i, result;

    if (!initialised) { set_msg(msg, msg_len, "Not initialised."); return 0; }

    sprintf(index_path, "%s/index.html", DIR_TEMP);
    result = ad_download_http_file_with_retries(DOWNLOAD_WEBSITE, index_path, TRUE);
    if (result != AD_SUCCESS) {
        set_msg(msg, msg_len, "Could not download the Retroplay index page.");
        return 0;
    }
    html_phase_extract_and_validate_links(packs, NUM_PACKS, DIR_TEMP, DIR_ZIP_FILES);

    /* Re-process every DAT, as ESTIMATESPACE does, so the lists exist even
     * when the zip did not change since the last run. */
    for (i = 0; i < NUM_PACKS; i++) packs[i].updated_dat_downloaded = 1;
    extract_Zip_file_and_rename(DIR_ZIP_FILES, packs, NUM_PACKS, options.verbose_output);
    process_and_archive_WHDLoadDat_Files(packs, NUM_PACKS);
    return 1;
}

int engine_install(int pack, const char *archive, unsigned long size, const char *crc)
{
    LONG rc;
    if (!initialised) return ENGINE_FAILED;
    rc = execute_archive_download_command(archive, (ULONG)size, crc,
                                          &packs[pack_index(pack)], &options, 0, NULL);
    if (rc == 2) return ENGINE_SKIPPED;
    if (rc == 20) return ENGINE_CANCELLED;
    return rc == 0 ? ENGINE_INSTALLED : ENGINE_FAILED;
}

/* ---- settings ---- */

#define INI_PATH "PROGDIR:whdfetch.ini"
#define NUM_KEYS 6

static char install_dir_buf[256];

void engine_get_settings(engine_settings *s)
{
    memset(s, 0, sizeof(*s));
    if (options.extract_path) {
        strncpy(s->install_dir, options.extract_path, sizeof(s->install_dir) - 1);
    }
    s->delete_archives = options.delete_archives_after_extract == TRUE;
    s->skip_aga = skip_AGA != 0;
    s->skip_cd = skip_CD != 0;
    s->skip_ntsc = skip_NTSC != 0;
    s->skip_non_english = skip_NonEnglish != 0;
}

/* "key" or "key  =..." at the start of an ini line (after blanks). */
static int line_has_key(const char *line, const char *key)
{
    size_t n = strlen(key);
    while (*line == ' ' || *line == '\t') line++;
    if (strnicmp_ascii(line, key, n) != 0) return 0;
    line += n;
    while (*line == ' ' || *line == '\t') line++;
    return *line == '=';
}

static int is_section(const char *line, const char **name_end)
{
    while (*line == ' ' || *line == '\t') line++;
    if (*line != '[') return 0;
    *name_end = strchr(line, ']');
    return *name_end != NULL;
}

static int is_global_section(const char *line)
{
    while (*line == ' ' || *line == '\t') line++;
    return strnicmp_ascii(line, "[global]", 8) == 0;
}

/* Rewrites our keys inside [global] and keeps every other line as it is. */
static int write_ini(const char *keys[], char values[][260], char *msg, int msg_len)
{
    static char line[1024];
    FILE *in, *out;
    int written[NUM_KEYS] = { 0 };
    int in_global = 0, seen_global = 0, i;
    const char *tmp = INI_PATH ".tmp";

    out = fopen(tmp, "w");
    if (!out) { set_msg(msg, msg_len, "Cannot write " INI_PATH); return 0; }
    in = fopen(INI_PATH, "r");

    while (in && fgets(line, sizeof(line), in)) {
        const char *end;
        if (is_section(line, &end)) {
            if (in_global) {
                for (i = 0; i < NUM_KEYS; i++)
                    if (!written[i]) { fprintf(out, "%s=%s\n", keys[i], values[i]); written[i] = 1; }
            }
            in_global = is_global_section(line);
            if (in_global) seen_global = 1;
            fputs(line, out);
            continue;
        }
        if (in_global) {
            int replaced = 0;
            for (i = 0; i < NUM_KEYS; i++) {
                if (line_has_key(line, keys[i])) {
                    if (!written[i]) fprintf(out, "%s=%s\n", keys[i], values[i]);
                    written[i] = 1;
                    replaced = 1;
                    break;
                }
            }
            if (replaced) continue;
        }
        fputs(line, out);
    }
    if (in) fclose(in);

    if (!seen_global) fputs("\n[global]\n", out);
    for (i = 0; i < NUM_KEYS; i++)
        if (!written[i]) fprintf(out, "%s=%s\n", keys[i], values[i]);

    if (fclose(out) != 0) { remove(tmp); set_msg(msg, msg_len, "Cannot write " INI_PATH); return 0; }
    remove(INI_PATH);
    if (rename(tmp, INI_PATH) != 0) { set_msg(msg, msg_len, "Cannot replace " INI_PATH); return 0; }
    return 1;
}

int engine_save_settings(const engine_settings *s, char *msg, int msg_len)
{
    static const char *keys[NUM_KEYS] = {
        "extract_path", "delete_archives_after_extract",
        "skip_aga", "skip_cd", "skip_ntsc", "skip_non_english"
    };
    static char values[NUM_KEYS][260];

    /* Apply now. */
    strncpy(install_dir_buf, s->install_dir, sizeof(install_dir_buf) - 1);
    install_dir_buf[sizeof(install_dir_buf) - 1] = 0;
    options.extract_path = install_dir_buf[0] ? install_dir_buf : NULL;
    options.delete_archives_after_extract = s->delete_archives ? TRUE : FALSE;
    skip_AGA = s->skip_aga;
    skip_CD = s->skip_cd;
    skip_NTSC = s->skip_ntsc;
    skip_NonEnglish = s->skip_non_english;

    strcpy(values[0], install_dir_buf);
    strcpy(values[1], s->delete_archives ? "true" : "false");
    strcpy(values[2], s->skip_aga ? "true" : "false");
    strcpy(values[3], s->skip_cd ? "true" : "false");
    strcpy(values[4], s->skip_ntsc ? "true" : "false");
    strcpy(values[5], s->skip_non_english ? "true" : "false");
    return write_ini(keys, values, msg, msg_len);
}

const char *engine_extract_target(void)
{
    if (options.extract_archives != TRUE) return "(extraction off in whdfetch.ini)";
    if (options.extract_path && options.extract_path[0]) return options.extract_path;
    return DIR_GAME_DOWNLOADS;
}
