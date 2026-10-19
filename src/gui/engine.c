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
#include "gui/modland.h"
#include "gui/scene.h"

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
static char modland_dir_buf[256]; /* modland_extract_path from whdfetch.ini */
static char player_path_buf[256]; /* player_path (HippoPlayer) from whdfetch.ini */
static char scene_dir_buf[256];   /* scene_extract_path from whdfetch.ini */
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
#define NUM_KEYS 10

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
    strncpy(s->modland_dir, modland_dir_buf, sizeof(s->modland_dir) - 1);
    strncpy(s->player_path, player_path_buf, sizeof(s->player_path) - 1);
    strncpy(s->scene_dir, scene_dir_buf, sizeof(s->scene_dir) - 1);
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
        "aminet_extract_path", "modland_extract_path", "player_path", "scene_extract_path"
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
    strncpy(modland_dir_buf, s->modland_dir, sizeof(modland_dir_buf) - 1);
    modland_dir_buf[sizeof(modland_dir_buf) - 1] = 0;
    strcpy(values[7], modland_dir_buf);
    strncpy(player_path_buf, s->player_path, sizeof(player_path_buf) - 1);
    player_path_buf[sizeof(player_path_buf) - 1] = 0;
    strcpy(values[8], player_path_buf);
    strncpy(scene_dir_buf, s->scene_dir, sizeof(scene_dir_buf) - 1);
    scene_dir_buf[sizeof(scene_dir_buf) - 1] = 0;
    strcpy(values[9], scene_dir_buf);
    return write_ini(keys, values, msg, msg_len);
}

/* whdfetch's own parser ignores keys it does not know, so the Aminet and
 * Modland targets are read here from the [global] section. */
static void read_aminet_setting(void)
{
    static char line[512];
    FILE *f = fopen(INI_PATH, "r");
    int in_global = 0;
    aminet_dir_buf[0] = 0;
    modland_dir_buf[0] = 0;
    player_path_buf[0] = 0;
    scene_dir_buf[0] = 0;
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        const char *end;
        char *target = NULL;
        if (is_section(line, &end)) { in_global = is_global_section(line); continue; }
        if (!in_global) continue;
        if (line_has_key(line, "aminet_extract_path")) target = aminet_dir_buf;
        else if (line_has_key(line, "modland_extract_path")) target = modland_dir_buf;
        else if (line_has_key(line, "player_path")) target = player_path_buf;
        else if (line_has_key(line, "scene_extract_path")) target = scene_dir_buf;
        if (target) {
            char *v = strchr(line, '=') + 1;
            size_t n;
            while (*v == ' ' || *v == '\t') v++;
            n = strlen(v);
            while (n > 0 && (v[n - 1] == '\n' || v[n - 1] == '\r' || v[n - 1] == ' ')) v[--n] = 0;
            strncpy(target, v, 255);
            target[255] = 0;
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

    /* RECENT first: small (about 10 KB) and the default view. */
    {
        char raw[64], list[64];
        sprintf(raw, "%s/RECENT", AMINET_DIR);
        sprintf(list, "%s/recent.txt", AMINET_DIR);
        printf("Downloading %s\n", AMINET_RECENT_URL);
        if (ad_download_http_file_with_retries(AMINET_RECENT_URL, raw, TRUE) == AD_SUCCESS) {
            count = aminet_convert_list(raw, list);
            printf("Recent uploads: %ld packages\n", count);
            DeleteFile((STRPTR)raw);
        } else {
            printf("Could not download RECENT, continuing with INDEX.gz\n");
        }
    }

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

static int install_package(const char *url, const char *target, const char *sub,
                           const char *temp_dir, const char *name);

int engine_install_aminet(const char *dir, const char *name)
{
    char url[300];
    if (strlen(dir) + strlen(name) + 40 > sizeof(url)) return ENGINE_FAILED;
    sprintf(url, "%s%s/%s", AMINET_BASE_URL, dir, name);
    return install_package(url, engine_aminet_target(), NULL, AMINET_DIR, name);
}

/* Downloads url and extracts it into <target>[<sub>/]<name without ext>/
 * (archives) or saves it there as it is (other files). Names are made
 * safe for FFS (30 characters, no spaces). */
static int install_package(const char *url, const char *target, const char *sub,
                           const char *temp_dir, const char *name)
{
    char archive[300], base[300], dest[400], cmd[800], stem[40], local[40];
    const char *tool;
    char *dot;
    LONG rc;

    if (!initialised) return ENGINE_FAILED;
    if (strchr(name, '"') || strchr(name, '/') || strchr(name, ':') || strlen(name) > 200)
        return ENGINE_FAILED;

    /* Archives are extracted; anything else (DMS/ADF disk images, pictures,
     * music, documents) is saved as it is into the package drawer. */
    if (ends_with(name, ".lha") || ends_with(name, ".lzh"))
        tool = "c:lha -T -M -N -m x \"%s\" \"%s/\"";
    else if (ends_with(name, ".lzx"))
        tool = "c:unlzx -m x \"%s\" \"%s/\"";
    else if (ends_with(name, ".zip"))
        tool = "unzip -o -qq \"%s\" -d \"%s\"";   /* same unzip whdfetch uses */
    else
        tool = NULL;

    /* Each package goes to its own drawer: <target>/<name without ext>. */
    modland_local_name(name, local);
    {
        char tmp[256];
        strncpy(tmp, name, sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = 0;
        dot = strrchr(tmp, '.');
        if (dot) *dot = 0;
        modland_local_name(tmp, stem);
    }
    strncpy(base, target, sizeof(base) - 40);
    base[sizeof(base) - 40] = 0;
    if (base[0] && base[strlen(base) - 1] != '/' && base[strlen(base) - 1] != ':') strcat(base, "/");
    if (sub && sub[0]) {
        /* e.g. Demos/<party>/: create the target first, then the sub drawer */
        char t[300], s[40];
        strcpy(t, base);
        if (t[0] && t[strlen(t) - 1] == '/') t[strlen(t) - 1] = 0;
        if (t[0] && t[strlen(t) - 1] != ':' && !ensure_dir(t)) {
            printf("Cannot create %s\n", t);
            return ENGINE_FAILED;
        }
        modland_local_name(sub, s);
        strcat(base, s);
        strcat(base, "/");
    }
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
    if (!ensure_dir(DIR_TEMP) || !ensure_dir(temp_dir) || !ensure_dir(dest)) {
        printf("Cannot create %s\n", dest);
        return ENGINE_FAILED;
    }

    if (tool) sprintf(archive, "%s/%s", temp_dir, local);
    else sprintf(archive, "%s/%s", dest, local);     /* kept as it is */
    printf("Downloading %s\n", url);
    rc = ad_download_http_file_with_retries(url, archive, TRUE);
    if (rc != AD_SUCCESS) {
        /* Remove the (still empty) drawer so a retry is not "installed". */
        DeleteFile((STRPTR)archive);
        DeleteFile((STRPTR)dest);
        if (rc == 20) return ENGINE_CANCELLED;
        printf("Download failed: %s\n", url);
        return ENGINE_FAILED;
    }

    if (!tool) {
        printf("Saved without extracting (not an archive): %s\n", archive);
        return ENGINE_INSTALLED;
    }

    sprintf(cmd, tool, archive, dest);
    printf("Extracting: %s\n", cmd);
    rc = SystemTagList((STRPTR)cmd, NULL);
    printf("Extractor returned %ld\n", (long)rc);
    if (options.delete_archives_after_extract == TRUE) DeleteFile((STRPTR)archive);
    if (rc != 0) {
        DeleteFile((STRPTR)dest);   /* only succeeds if nothing was extracted */
        return ENGINE_FAILED;
    }
    printf("Installed to %s\n", dest);
    return ENGINE_INSTALLED;
}

/* ---- Scene.org parties ---- */

#define SCENE_DIR     "temp/Scene"
#define SCENE_DEFAULT "Demos/"
#define SCENE_MAX_DIRS 400

typedef struct {
    char (*names)[64];
    int count;
    int amiga_only;     /* keep only directories with "amiga" in the name */
} dir_list;

static int collect_dir(const char *name, int is_dir, unsigned long size, void *user)
{
    dir_list *d = (dir_list *)user;
    if (!is_dir || d->count >= SCENE_MAX_DIRS || strlen(name) >= 64) return 0;
    if (d->amiga_only && !scene_is_amiga_dir(name)) return 0;
    strcpy(d->names[d->count++], name);
    return 0;
}

typedef struct {
    FILE *out;
    const char *party;
    const char *prefix;     /* "parties/<year>/<party>/<compo>" */
    long count;
} file_sink;

static int collect_file(const char *name, int is_dir, unsigned long size, void *user)
{
    file_sink *s = (file_sink *)user;
    if (is_dir || !scene_is_entry_file(name)) return 0;
    /* name<TAB>party<TAB>size<TAB>0<TAB>path (GL_FMT_AMINET layout) */
    fprintf(s->out, "%s\t%s\t%lu\t0\t%s/%s\n", name, s->party, size, s->prefix, name);
    s->count++;
    return 0;
}

/* Downloads one directory page of the party archive. */
static int get_listing(const char *rel, const char *file)
{
    char url[400], enc[340];
    if (!modland_url_encode(rel, enc, sizeof(enc))) return 0;
    sprintf(url, "%s%s/", SCENE_BASE_URL, enc);
    return ad_download_http_file_with_retries(url, file, TRUE) == AD_SUCCESS;
}

const char *engine_scene_dir(void) { return SCENE_DIR; }

const char *engine_scene_target(void)
{
    return scene_dir_buf[0] ? scene_dir_buf : SCENE_DEFAULT;
}

int engine_refresh_scene(int year, char *msg, int msg_len)
{
    static char parties[SCENE_MAX_DIRS][64], compos[SCENE_MAX_DIRS][64], subs[SCENE_MAX_DIRS][64];
    char rel[300], list_path[64], tmp_path[64], page[64];
    dir_list pl, cl, sl;
    file_sink sink;
    int p, c, s;

    if (!initialised) { set_msg(msg, msg_len, "Not initialised."); return 0; }
    if (!ensure_dir(DIR_TEMP) || !ensure_dir(SCENE_DIR)) {
        set_msg(msg, msg_len, "Cannot create " SCENE_DIR);
        return 0;
    }
    sprintf(page, "%s/page.html", SCENE_DIR);
    sprintf(list_path, "%s/%d.txt", SCENE_DIR, year);
    sprintf(tmp_path, "%s.tmp", list_path);

    sprintf(rel, "parties/%d", year);
    printf("Reading %s%s/\n", SCENE_BASE_URL, rel);
    if (!get_listing(rel, page)) {
        set_msg(msg, msg_len, "Could not read the scene.org party list for that year.");
        return 0;
    }
    pl.names = parties; pl.count = 0; pl.amiga_only = 0;
    scene_parse_listing(page, collect_dir, &pl);
    printf("%d parties in %d\n", pl.count, year);

    sink.out = fopen(tmp_path, "w");
    if (!sink.out) { set_msg(msg, msg_len, "Cannot write " SCENE_DIR); return 0; }
    sink.count = 0;

    for (p = 0; p < pl.count; p++) {
        if (SetSignal(0, 0) & SIGBREAKF_CTRL_C) break;
        sprintf(rel, "parties/%d/%s", year, parties[p]);
        if (!get_listing(rel, page)) { printf("Skipped %s (download failed)\n", parties[p]); continue; }
        cl.names = compos; cl.count = 0; cl.amiga_only = 1;
        scene_parse_listing(page, collect_dir, &cl);
        for (c = 0; c < cl.count; c++) {
            char prefix[300];
            sprintf(prefix, "parties/%d/%s/%s", year, parties[p], compos[c]);
            if (!get_listing(prefix, page)) continue;
            printf("%s: %s\n", parties[p], compos[c]);
            sink.party = parties[p];
            sink.prefix = prefix;
            scene_parse_listing(page, collect_file, &sink);
            /* Entries in their own sub-drawers (one level down). */
            sl.names = subs; sl.count = 0; sl.amiga_only = 0;
            scene_parse_listing(page, collect_dir, &sl);
            for (s = 0; s < sl.count; s++) {
                char sub_prefix[300];
                if (strlen(prefix) + strlen(subs[s]) + 2 >= sizeof(sub_prefix)) continue;
                sprintf(sub_prefix, "%s/%s", prefix, subs[s]);
                if (!get_listing(sub_prefix, page)) continue;
                sink.prefix = sub_prefix;
                scene_parse_listing(page, collect_file, &sink);
            }
        }
    }
    DeleteFile((STRPTR)page);
    if (fclose(sink.out) != 0) { remove(tmp_path); set_msg(msg, msg_len, "Cannot write the list."); return 0; }
    remove(list_path);
    if (rename(tmp_path, list_path) != 0) { set_msg(msg, msg_len, "Cannot write the list."); return 0; }
    printf("Scene.org %d: %ld Amiga entries\n", year, sink.count);
    return 1;
}

int engine_install_scene(const char *path, const char *party, const char *name)
{
    char url[500], enc[420];
    if (!modland_url_encode(path, enc, sizeof(enc))) return ENGINE_FAILED;
    sprintf(url, "%s%s", SCENE_BASE_URL, enc);
    return install_package(url, engine_scene_target(), party, SCENE_DIR, name);
}

/* ---- Modland ---- */

#define MODLAND_DIR     "temp/Modland"
#define MODLAND_DEFAULT "Music/"

const char *engine_modland_dir(void) { return MODLAND_DIR; }

const char *engine_modland_target(void)
{
    return modland_dir_buf[0] ? modland_dir_buf : MODLAND_DEFAULT;
}

int engine_refresh_modland(char *msg, int msg_len)
{
    char zip[64];
    long count;

    if (!initialised) { set_msg(msg, msg_len, "Not initialised."); return 0; }
    if (!ensure_dir(DIR_TEMP) || !ensure_dir(MODLAND_DIR)) {
        set_msg(msg, msg_len, "Cannot create " MODLAND_DIR);
        return 0;
    }
    sprintf(zip, "%s/allmods.zip", MODLAND_DIR);
    printf("Downloading %s\n", MODLAND_INDEX_URL);
    if (ad_download_http_file_with_retries(MODLAND_INDEX_URL, zip, TRUE) != AD_SUCCESS) {
        set_msg(msg, msg_len, "Could not download the Modland index (allmods.zip).");
        return 0;
    }
    printf("Unpacking and sorting allmods.zip (Amiga formats only)\n");
    count = modland_split_index(zip, MODLAND_DIR);
    DeleteFile((STRPTR)zip);   /* 5.8 MB, only needed for the split */
    if (count < 0) {
        set_msg(msg, msg_len, "Could not read allmods.zip or write the lists.");
        return 0;
    }
    printf("Modland: %ld Amiga modules\n", count);
    return 1;
}

const char *engine_player_path(void) { return player_path_buf; }

/* Full path of a downloaded module, as engine_install_modland saves it. */
int engine_modland_file(const char *author, const char *name, char *out, int len)
{
    char base[260], local[40], auth[40], rel[340];
    BPTR lock;
    int ok;

    strncpy(base, engine_modland_target(), sizeof(base) - 2);
    base[sizeof(base) - 2] = 0;
    if (base[0] && base[strlen(base) - 1] != '/' && base[strlen(base) - 1] != ':') strcat(base, "/");
    modland_local_name(author[0] ? author : "Unknown", auth);
    modland_local_name(name, local);
    sprintf(rel, "%s%s/%s", base, auth, local);

    /* Programs started elsewhere (HippoPlayer) need an absolute path. */
    lock = Lock((STRPTR)rel, ACCESS_READ);
    if (!lock) return 0;
    ok = NameFromLock(lock, (STRPTR)out, len);
    UnLock(lock);
    return ok;
}

static int install_modland_file(const char *path, const char *author, const char *name);

/* Downloads a module; two-file formats (TFMX mdat/smpl) get both files. */
int engine_install_modland(const char *path, const char *author, const char *name)
{
    char second[600];
    int r = install_modland_file(path, author, name);
    if ((r == ENGINE_INSTALLED || r == ENGINE_SKIPPED) &&
        modland_companion(path, second, sizeof(second))) {
        const char *second_name = strrchr(second, '/') + 1;
        int r2;
        printf("Also needed: %s\n", second_name);
        r2 = install_modland_file(second, author, second_name);
        if (r2 == ENGINE_CANCELLED) return ENGINE_CANCELLED;
        if (r2 == ENGINE_FAILED) {
            printf("Sample file missing, the module will not play: %s\n", second_name);
            return ENGINE_FAILED;
        }
        if (r == ENGINE_SKIPPED && r2 == ENGINE_INSTALLED) r = ENGINE_INSTALLED;
    }
    return r;
}

static int install_modland_file(const char *path, const char *author, const char *name)
{
    char url[600], enc[520], base[260], dir[300], file[340], local[40], auth[40];
    LONG rc;

    if (!initialised) return ENGINE_FAILED;
    if (!modland_url_encode(path, enc, sizeof(enc))) return ENGINE_FAILED;
    sprintf(url, "%s%s", MODLAND_BASE_URL, enc);

    strncpy(base, engine_modland_target(), sizeof(base) - 2);
    base[sizeof(base) - 2] = 0;
    if (base[0] && base[strlen(base) - 1] != '/' && base[strlen(base) - 1] != ':') strcat(base, "/");

    /* <target>/<author>/<module>, names shortened for FFS. */
    modland_local_name(author[0] ? author : "Unknown", auth);
    modland_local_name(name, local);
    {
        char b[260];
        strcpy(b, base);
        if (b[0] && b[strlen(b) - 1] == '/') b[strlen(b) - 1] = 0;
        if (b[0] && b[strlen(b) - 1] != ':' && !ensure_dir(b)) {
            printf("Cannot create %s\n", b);
            return ENGINE_FAILED;
        }
    }
    sprintf(dir, "%s%s", base, auth);
    sprintf(file, "%s/%s", dir, local);
    if (path_exists(file)) {
        printf("Already there: %s\n", file);
        return ENGINE_SKIPPED;
    }
    if (!ensure_dir(dir)) {
        printf("Cannot create %s\n", dir);
        return ENGINE_FAILED;
    }
    printf("Downloading %s\n", url);
    rc = ad_download_http_file_with_retries(url, file, TRUE);
    if (rc != AD_SUCCESS) {
        DeleteFile((STRPTR)file);
        DeleteFile((STRPTR)dir);    /* only if it is still empty */
        if (rc == 20) return ENGINE_CANCELLED;
        printf("Download failed: %s\n", url);
        return ENGINE_FAILED;
    }
    printf("Saved %s\n", file);
    return ENGINE_INSTALLED;
}

const char *engine_extract_target(void)
{
    if (options.extract_archives != TRUE) return "(extraction off in whdfetch.ini)";
    if (options.extract_path && options.extract_path[0]) return options.extract_path;
    return DIR_GAME_DOWNLOADS;
}
