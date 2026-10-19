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
#include "gui/aminet.h"
#include "gui/inflate.h"

#include <dos/dostags.h>
#include <proto/dos.h>

#define NUM_PACKS 5
#define AMINET_DIR      "temp/Aminet"
#define AMINET_DEFAULT  "Aminet/"

static struct whdload_pack_def packs[NUM_PACKS];
static struct download_option options;
static BOOL download_lib_ready = FALSE;
static int initialised = 0;      /* network ready */
static int config_loaded = 0;    /* defaults + whdfetch.ini loaded */
static char aminet_dir_buf[256]; /* aminet_extract_path from whdfetch.ini */
static void read_aminet_setting(void);

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
        read_aminet_setting();
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
#define NUM_KEYS 7

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
    strncpy(s->aminet_dir, aminet_dir_buf, sizeof(s->aminet_dir) - 1);
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
        "skip_aga", "skip_cd", "skip_ntsc", "skip_non_english",
        "aminet_extract_path"
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
    strncpy(aminet_dir_buf, s->aminet_dir, sizeof(aminet_dir_buf) - 1);
    aminet_dir_buf[sizeof(aminet_dir_buf) - 1] = 0;
    strcpy(values[6], aminet_dir_buf);
    return write_ini(keys, values, msg, msg_len);
}

/* whdfetch's own parser ignores keys it does not know, so the Aminet
 * target is read here from the [global] section. */
static void read_aminet_setting(void)
{
    static char line[512];
    FILE *f = fopen(INI_PATH, "r");
    int in_global = 0;
    aminet_dir_buf[0] = 0;
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        const char *end;
        if (is_section(line, &end)) { in_global = is_global_section(line); continue; }
        if (in_global && line_has_key(line, "aminet_extract_path")) {
            char *v = strchr(line, '=') + 1;
            size_t n;
            while (*v == ' ' || *v == '\t') v++;
            n = strlen(v);
            while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r' || v[n - 1] == ' ')) v[--n] = 0;
            strncpy(aminet_dir_buf, v, sizeof(aminet_dir_buf) - 1);
            aminet_dir_buf[sizeof(aminet_dir_buf) - 1] = 0;
        }
    }
    fclose(f);
}

/* ---- Aminet ---- */


static int ensure_dir(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (lock) { UnLock(lock); return 1; }
    lock = CreateDir((STRPTR)path);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

static int path_exists(const char *path)
{
    BPTR lock = Lock((STRPTR)path, ACCESS_READ);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

int engine_refresh_aminet(char *msg, int msg_len)
{
    char gz[64], index[64];
    int r;
    long count;

    if (!initialised) { set_msg(msg, msg_len, "Not initialised."); return 0; }
    if (!ensure_dir(DIR_TEMP) || !ensure_dir(AMINET_DIR)) {
        set_msg(msg, msg_len, "Cannot create " AMINET_DIR);
        return 0;
    }
    sprintf(gz, "%s/INDEX.gz", AMINET_DIR);
    sprintf(index, "%s/INDEX", AMINET_DIR);

    printf("Downloading %s\n", AMINET_INDEX_URL);
    if (ad_download_http_file_with_retries(AMINET_INDEX_URL, gz, TRUE) != AD_SUCCESS) {
        set_msg(msg, msg_len, "Could not download the Aminet index (INDEX.gz).");
        return 0;
    }
    printf("Unpacking INDEX.gz\n");
    r = inflate_gunzip_file(gz, index);
    if (r != IF_OK) {
        sprintf(msg, "Unpacking INDEX.gz failed (code %d).", r);
        return 0;
    }
    count = aminet_split_index(index, AMINET_DIR);
    DeleteFile((STRPTR)index);    /* 7 MB, only needed for the split */
    if (count < 0) {
        set_msg(msg, msg_len, "Could not write the Aminet category lists.");
        return 0;
    }
    printf("Aminet index: %ld packages\n", count);
    return 1;
}

const char *engine_aminet_target(void)
{
    return aminet_dir_buf[0] ? aminet_dir_buf : AMINET_DEFAULT;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n > m && strnicmp_ascii(s + n - m, suffix, m) == 0;
}

int engine_install_aminet(const char *dir, const char *name)
{
    char url[300], archive[300], base[260], dest[400], cmd[800], stem[128];
    const char *tool;
    char *dot;
    LONG rc;

    if (!initialised) return ENGINE_FAILED;
    if (strchr(name, '"') || strchr(name, '/') || strchr(name, ':') || strlen(name) > 100)
        return ENGINE_FAILED;

    if (ends_with(name, ".lha") || ends_with(name, ".lzh"))
        tool = "c:lha -T -M -N -m x \"%s\" \"%s/\"";
    else if (ends_with(name, ".lzx"))
        tool = "c:unlzx -m x \"%s\" \"%s/\"";
    else {
        printf("Not an LhA/LZX archive, cannot extract: %s\n", name);
        return ENGINE_FAILED;
    }

    /* Each package goes to its own drawer: <target>/<name without ext>. */
    strcpy(stem, name);
    dot = strrchr(stem, '.');
    if (dot) *dot = 0;
    strncpy(base, engine_aminet_target(), sizeof(base) - 1);
    base[sizeof(base) - 1] = 0;
    if (base[0] && base[strlen(base) - 1] != '/' && base[strlen(base) - 1] != ':') strcat(base, "/");
    sprintf(dest, "%s%s", base, stem);
    if (path_exists(dest)) {
        printf("Already installed: %s\n", dest);
        return ENGINE_SKIPPED;
    }
    {
        /* Create the target drawer itself (without the trailing '/'). */
        char b[260];
        strcpy(b, base);
        if (b[0] && b[strlen(b) - 1] == '/') b[strlen(b) - 1] = 0;
        if (b[0] && b[strlen(b) - 1] != ':' && !ensure_dir(b)) {
            printf("Cannot create %s\n", b);
            return ENGINE_FAILED;
        }
    }
    if (!ensure_dir(DIR_TEMP) || !ensure_dir(AMINET_DIR) || !ensure_dir(dest)) {
        printf("Cannot create %s\n", dest);
        return ENGINE_FAILED;
    }

    sprintf(url, "%s%s/%s", AMINET_BASE_URL, dir, name);
    sprintf(archive, "%s/%s", AMINET_DIR, name);
    printf("Downloading %s\n", url);
    rc = ad_download_http_file_with_retries(url, archive, TRUE);
    if (rc == 20) return ENGINE_CANCELLED;
    if (rc != AD_SUCCESS) {
        printf("Download failed: %s\n", url);
        return ENGINE_FAILED;
    }

    sprintf(cmd, tool, archive, dest);
    printf("Extracting: %s\n", cmd);
    rc = SystemTagList((STRPTR)cmd, NULL);
    printf("Extractor returned %ld\n", (long)rc);
    if (options.delete_archives_after_extract == TRUE) DeleteFile((STRPTR)archive);
    if (rc != 0) return ENGINE_FAILED;
    printf("Installed to %s\n", dest);
    return ENGINE_INSTALLED;
}

const char *engine_extract_target(void)
{
    if (options.extract_archives != TRUE) return "(extraction off in whdfetch.ini)";
    if (options.extract_path && options.extract_path[0]) return options.extract_path;
    return DIR_GAME_DOWNLOADS;
}
