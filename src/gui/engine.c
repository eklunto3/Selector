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
static int initialised = 0;

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

    setup_app_defaults(packs, &options);
    report_init();
    ini_parser_load_overrides(packs, &options);
    ensure_time_zone_set();
    start_time = time(NULL);

    /* The GUI decides per archive what to fetch; keep all packs active so
     * refresh processes every DAT. */
    for (i = 0; i < NUM_PACKS; i++) packs[i].user_requested_download = 1;

    if (!ad_init_download_lib_taglist(
            ADTAG_Verbose, FALSE,
            ADTAG_Timeout, options.timeout_seconds,
            ADTAG_BufferSize, 16384,
            ADTAG_UserAgent, "whdfetch/1.0",
            ADTAG_MaxRetries, 2,
            TAG_DONE)) {
        set_msg(msg, msg_len, "Cannot open bsdsocket.library. Is the TCP/IP stack running?");
        lifecycle_do_shutdown(&download_lib_ready);
        return 0;
    }
    download_lib_ready = TRUE;
    initialised = 1;
    return 1;
}

void engine_shutdown(void)
{
    if (!initialised) return;
    lifecycle_do_shutdown(&download_lib_ready);
    initialised = 0;
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

const char *engine_extract_target(void)
{
    if (options.extract_archives != TRUE) return "(extraction off in whdfetch.ini)";
    if (options.extract_path && options.extract_path[0]) return options.extract_path;
    return DIR_GAME_DOWNLOADS;
}
