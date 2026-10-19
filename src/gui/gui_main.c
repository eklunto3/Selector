/* WHDSelector - MUI front end on top of whdfetch (step G1).
 *
 * G1: pick a pack, browse its archive list, search and filter by first
 * letter. The list comes from the text files whdfetch's DAT phase writes
 * to "temp/Dat files/"; "Refresh list" runs whdfetch itself to update them
 * (DOWNLOADALL ESTIMATESPACE fetches DATs only, no game archives).
 *
 * MUI 3.8 features and NList.mcc only. All UI texts are English. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <libraries/mui.h>
#include <libraries/asl.h>
#include <mui/NListview_mcc.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>

#include "gui/game_list.h"
#include "gamefile_parser.h"
#include "gui/engine.h"
#include "gui/activity_log.h"
#include "gui/aminet.h"

extern const char *WHDLOAD_FILE_FILTER_DAT_GAMES;
extern const char *WHDLOAD_FILE_FILTER_DAT_GAMES_BETA_AND_UNRELEASED;
extern const char *WHDLOAD_FILE_FILTER_DAT_DEMOS;
extern const char *WHDLOAD_FILE_FILTER_DAT_BETA_AND_UNRELEASED;
extern const char *WHDLOAD_FILE_FILTER_DAT_MAGAZINES;
extern const char *DIR_DAT_FILES;

size_t __stack = 131072;   /* same as whdfetch */
static const char version[] = "$VER: WHDSelector 0.9 (19.10.2026)";

struct Library *MUIMasterBase;

#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))

enum { ID_PACK = 1, ID_SEARCH, ID_LETTER, ID_REFRESH, ID_DOWNLOAD, ID_STOP,
       ID_SETTINGS, ID_SET_SAVE, ID_SET_CANCEL, ID_SOURCE };

/* ---- packs ---- */

static const char *pack_labels[] = {
    "Games", "Games (Beta & Unofficial)", "Demos", "Demos (Beta & Unofficial)", "Magazines", NULL
};

static const char *pack_filter(int index)
{
    switch (index) {
    case 0: return WHDLOAD_FILE_FILTER_DAT_GAMES;
    case 1: return WHDLOAD_FILE_FILTER_DAT_GAMES_BETA_AND_UNRELEASED;
    case 2: return WHDLOAD_FILE_FILTER_DAT_DEMOS;
    case 3: return WHDLOAD_FILE_FILTER_DAT_BETA_AND_UNRELEASED;
    default: return WHDLOAD_FILE_FILTER_DAT_MAGAZINES;
    }
}

/* "All", "0-9", "A".."Z" */
static char letter_store[26][2];
static const char *letter_labels[29];

static void init_letters(void)
{
    int i;
    letter_labels[0] = "All";
    letter_labels[1] = "0-9";
    for (i = 0; i < 26; i++) {
        letter_store[i][0] = (char)('A' + i);
        letter_store[i][1] = 0;
        letter_labels[i + 2] = letter_store[i];
    }
    letter_labels[28] = NULL;
}

static int letter_value(LONG active)
{
    if (active <= 0) return GL_LETTER_ALL;
    if (active == 1) return GL_LETTER_DIGITS;
    return 'A' + (int)(active - 2);
}

/* ---- state ---- */

static Object *app, *win, *cy_pack, *str_search, *cy_letter, *lv_list, *lst, *txt_status, *bt_refresh;
static Object *bt_download, *bt_stop, *gauge, *bt_settings, *lst_log;
static Object *win_set, *str_dir, *cm_delete, *cm_aga, *cm_cd, *cm_ntsc, *cm_eng, *bt_save, *bt_cancel;
static int engine_ok, quit_after_download;
static char engine_msg[120];
static gl_list games;
static gl_entry **shown;
static char list_file[256];

/* ---- sources ---- */

#define SRC_RETROPLAY 0
#define SRC_AMINET    1
static int source = SRC_RETROPLAY;
static Object *cy_source, *grp_pages, *cy_cat, *str_amidir;

static const char *source_labels[] = { "Retroplay WHDLoad", "Aminet", NULL };

#define FORMAT_RETROPLAY "DELTA=16 W=55,DELTA=16 W=10,DELTA=16 W=25,P=\33r W=10"
#define FORMAT_AMINET    "DELTA=16 W=20,DELTA=16 W=11,DELTA=16 P=\33r W=7,DELTA=16 W=9,W=53"

/* Aminet "category" gadget: Recent first (the default view, like the
 * Aminet front page), then the 16 top-level categories. */
static const char *cat_labels[AMINET_NUM_CATEGORIES + 2];

static void init_cat_labels(void)
{
    int i;
    cat_labels[0] = "Recent (last 14 days)";
    for (i = 0; i < AMINET_NUM_CATEGORIES; i++) cat_labels[i + 1] = aminet_categories[i];
    cat_labels[AMINET_NUM_CATEGORIES + 1] = NULL;
}

/* ---- NList display hook ---- */

static void add_info(char *info, const char *part)
{
    if (info[0]) strcat(info, ", ");
    strcat(info, part);
}

/* Columns: title, version, info (only what differs from the usual
 * OCS / PAL / floppy / English defaults), size. The archive name is
 * split with whdfetch's own parser (gamefile_parser.c). */
static ULONG display_func(__reg("a0") struct Hook *hook, __reg("a2") char **array,
                          __reg("a1") gl_entry *e)
{
    static char size_buf[16];
    static char info[300];
    static game_metadata meta;

    if (source == SRC_AMINET) {
        /* Aminet columns: name, category, size, age, description. */
        static char age_buf[16];
        if (!e) {
            array[0] = "\33bName";
            array[1] = "\33bCategory";
            array[2] = "\33bSize";
            array[3] = "\33bAge";
            array[4] = "\33bDescription";
            return 0;
        }
        if (e->size >= 1024UL * 1024UL)
            sprintf(size_buf, "%lu.%lu MB", e->size / (1024UL * 1024UL),
                    (e->size % (1024UL * 1024UL)) * 10 / (1024UL * 1024UL));
        else
            sprintf(size_buf, "%lu KB", (e->size + 1023) / 1024);
        array[0] = (char *)e->name;
        array[1] = (char *)e->dir;
        array[2] = size_buf;
        array[3] = (char *)aminet_age_text(e->age, age_buf);
        array[4] = (char *)e->desc;
        return 0;
    }

    if (!e) {
        array[0] = "\33bTitle";
        array[1] = "\33bVersion";
        array[2] = "\33bInfo";
        array[3] = "\33bSize";
        return 0;
    }
    memset(&meta, 0, sizeof(meta));
    extract_game_info_from_filename(e->name, &meta);

    info[0] = 0;
    if (strcmp(meta.machineType, "OCS") != 0) add_info(info, meta.machineType);
    if (strcmp(meta.mediaFormat, "Floppy") != 0) add_info(info, meta.mediaFormat);
    if (strcmp(meta.videoFormat, "PAL") != 0) add_info(info, meta.videoFormat);
    if (strcmp(meta.language, "English") != 0 && strlen(info) + strlen(meta.language) < 200)
        add_info(info, meta.language);

    if (e->size >= 1024UL * 1024UL)
        sprintf(size_buf, "%lu.%lu MB", e->size / (1024UL * 1024UL),
                (e->size % (1024UL * 1024UL)) * 10 / (1024UL * 1024UL));
    else
        sprintf(size_buf, "%lu KB", (e->size + 1023) / 1024);

    array[0] = meta.title[0] ? meta.title : (char *)e->name;
    array[1] = meta.version;
    array[2] = info;
    array[3] = size_buf;
    return 0;
}

static struct Hook display_hook = { { NULL, NULL }, (HOOKFUNC)display_func, NULL, NULL };

/* ---- list file lookup ---- */

static int ends_with_txt(const char *n, size_t l)
{
    const char *e = n + l - 4;
    return e[0] == '.' && (e[1] | 32) == 't' && (e[2] | 32) == 'x' && (e[3] | 32) == 't';
}

/* Finds the newest "<filter>...txt" in DIR_DAT_FILES (dates sort as text). */
static int find_list_file(const char *filter, char *out, int outlen)
{
    struct FileInfoBlock *fib;
    BPTR lock;
    char best[108];
    int found = 0;

    lock = Lock((STRPTR)DIR_DAT_FILES, ACCESS_READ);
    if (!lock) return 0;
    fib = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (fib && Examine(lock, fib)) {
        while (ExNext(lock, fib)) {
            const char *n = (const char *)fib->fib_FileName;
            size_t l = strlen(n);
            if (fib->fib_DirEntryType >= 0 || l < 5) continue;
            if (!ends_with_txt(n, l)) continue;
            if (strncmp(n, filter, strlen(filter)) != 0) continue;
            if (!found || strcmp(n, best) > 0) {
                strncpy(best, n, sizeof(best) - 1);
                best[sizeof(best) - 1] = 0;
                found = 1;
            }
        }
    }
    if (fib) FreeDosObject(DOS_FIB, fib);
    UnLock(lock);
    if (found && (int)(strlen(DIR_DAT_FILES) + strlen(best) + 2) <= outlen) {
        sprintf(out, "%s/%s", DIR_DAT_FILES, best);
        return 1;
    }
    return 0;
}

/* ---- list handling ---- */

static void set_status(const char *text)
{
    set(txt_status, MUIA_Text_Contents, (ULONG)text);
}

static void apply_filter(void)
{
    static char status[200];
    STRPTR search = NULL;
    LONG letter = 0;
    long n = 0;

    get(str_search, MUIA_String_Contents, &search);
    get(cy_letter, MUIA_Cycle_Active, &letter);

    set(lst, MUIA_NList_Quiet, TRUE);
    DoMethod(lst, MUIM_NList_Clear);
    if (games.count > 0 && shown) {
        n = gl_filter(&games, search ? (const char *)search : "", letter_value(letter), shown);
        if (n > 0) DoMethod(lst, MUIM_NList_Insert, (ULONG)shown, n, MUIV_NList_Insert_Bottom);
    }
    set(lst, MUIA_NList_Quiet, FALSE);

    if (games.count > 0)
        sprintf(status, "Showing %ld of %ld  (%s)", n, games.count, FilePart((STRPTR)list_file));
    else
        sprintf(status, "No list found. Press \"Refresh list\".");
    set_status(status);
}

static void load_pack(void)
{
    LONG pack = 0;
    int found;

    free(shown);
    shown = NULL;
    gl_free(&games);
    list_file[0] = 0;

    if (source == SRC_AMINET) {
        get(cy_cat, MUIA_Cycle_Active, &pack);
        if (pack == 0) strcpy(list_file, "temp/Aminet/recent.txt");
        else sprintf(list_file, "temp/Aminet/%s.txt", aminet_categories[pack - 1]);
        found = gl_load(&games, list_file, GL_FMT_AMINET);
    } else {
        get(cy_pack, MUIA_Cycle_Active, &pack);
        found = find_list_file(pack_filter((int)pack), list_file, sizeof(list_file)) &&
                gl_load(&games, list_file, GL_FMT_RETROPLAY);
    }
    if (found) {
        shown = (gl_entry **)malloc((size_t)(games.count + 1) * sizeof(gl_entry *));
        if (!shown) gl_free(&games);
    }
    apply_filter();
}

/* Switches between Retroplay and Aminet: category gadget, columns, list. */
static void switch_source(void)
{
    LONG s = 0;
    get(cy_source, MUIA_Cycle_Active, &s);
    source = (int)s;
    set(grp_pages, MUIA_Group_ActivePage, s);
    set(lst, MUIA_NList_Format, (ULONG)(source == SRC_AMINET ? FORMAT_AMINET : FORMAT_RETROPLAY));
    load_pack();
    alog("Source: %s", source_labels[source]);
}

/* Downloads the latest DATs with whdfetch's own code (no games). */
static void refresh_lists(void)
{
    static char msg[120];

    if (!engine_ok) engine_ok = engine_init(engine_msg, sizeof(engine_msg));
    if (!engine_ok) {
        alog("%s", engine_msg);
        set_status(engine_msg);
        return;
    }
    set(app, MUIA_Application_Sleep, TRUE);
    msg[0] = 0;
    if (source == SRC_AMINET) {
        set_status("Fetching the Aminet index (about 1.9 MB, this takes a while)...");
        alog("Refresh: downloading Aminet INDEX.gz...");
    } else {
        set_status("Fetching the latest lists...");
        alog("Refresh: downloading index page and DAT files...");
    }
    alog_capture_begin();
    if (!(source == SRC_AMINET ? engine_refresh_aminet(msg, sizeof(msg))
                               : engine_refresh_lists(msg, sizeof(msg)))) {
        alog_capture_end();
        alog("Refresh failed: %s", msg);
        set(app, MUIA_Application_Sleep, FALSE);
        set_status(msg);
        return;
    }
    alog_capture_end();
    alog("Refresh: lists updated.");
    set(app, MUIA_Application_Sleep, FALSE);
    load_pack();
}

/* Handles pending input without waiting; returns 1 if the user pressed
 * Stop or closed the window. */
static int poll_stop(void)
{
    ULONG sigs = 0;
    LONG id;
    int stop = 0;
    while ((id = (LONG)DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs)) != 0) {
        if (id == ID_STOP) stop = 1;
        if (id == (LONG)MUIV_Application_ReturnID_Quit) { stop = 1; quit_after_download = 1; }
    }
    if (SetSignal(0, SIGBREAKF_CTRL_C) & SIGBREAKF_CTRL_C) stop = 1;
    return stop;
}

/* Downloads and extracts the selected archives one by one with whdfetch's
 * per-archive routine. Stop takes effect between archives. */
static void download_selected(void)
{
    static char status[300];
    gl_entry **todo;
    LONG pos = MUIV_NList_NextSelected_Start, pack = 0, n = 0, i;
    int installed = 0, skipped = 0, failed = 0, stopped = 0;

    if (!engine_ok) engine_ok = engine_init(engine_msg, sizeof(engine_msg));
    if (!engine_ok) {
        alog("%s", engine_msg);
        set_status(engine_msg);
        return;
    }
    todo = (gl_entry **)malloc((size_t)(games.count + 1) * sizeof(gl_entry *));
    if (!todo) {
        set_status("Out of memory.");
        return;
    }
    for (;;) {
        gl_entry *e = NULL;
        DoMethod(lst, MUIM_NList_NextSelected, (ULONG)&pos);
        if (pos == MUIV_NList_NextSelected_End) break;
        DoMethod(lst, MUIM_NList_GetEntry, pos, (ULONG)&e);
        if (e) todo[n++] = e;
    }
    if (n == 0) {
        free(todo);
        set_status("Select one or more titles first (Shift or Ctrl + click).");
        return;
    }
    get(cy_pack, MUIA_Cycle_Active, &pack);

    set(bt_download, MUIA_Disabled, TRUE);
    set(bt_refresh, MUIA_Disabled, TRUE);
    set(cy_source, MUIA_Disabled, TRUE);
    set(grp_pages, MUIA_Disabled, TRUE);
    set(bt_stop, MUIA_Disabled, FALSE);
    set(gauge, MUIA_Gauge_Max, n);
    set(gauge, MUIA_Gauge_Current, 0);

    for (i = 0; i < n; i++) {
        int r;
        sprintf(status, "Downloading %ld of %ld: %.200s", i + 1, n, todo[i]->name);
        set_status(status);
        if (poll_stop()) { stopped = 1; break; }

        alog("Download %ld/%ld: %s (%lu KB)", i + 1, n, todo[i]->name,
             (todo[i]->size + 1023) / 1024);
        alog_capture_begin();
        if (source == SRC_AMINET)
            r = engine_install_aminet(todo[i]->dir, todo[i]->name);
        else
            r = engine_install((int)pack, todo[i]->name, todo[i]->size, todo[i]->crc);
        alog_capture_end();
        if (r == ENGINE_INSTALLED) { installed++; alog("OK: %s downloaded and extracted", todo[i]->name); }
        else if (r == ENGINE_SKIPPED) { skipped++; alog("Skipped: %s is already there", todo[i]->name); }
        else if (r == ENGINE_CANCELLED) { stopped = 1; alog("Cancelled: %s", todo[i]->name); break; }
        else { failed++; alog("FAILED: %s (see lines above)", todo[i]->name); }
        set(gauge, MUIA_Gauge_Current, i + 1);
    }
    if (stopped && i < n) alog("Stopped by user.");

    set(bt_stop, MUIA_Disabled, TRUE);
    set(bt_download, MUIA_Disabled, FALSE);
    set(bt_refresh, MUIA_Disabled, FALSE);
    set(cy_source, MUIA_Disabled, FALSE);
    set(grp_pages, MUIA_Disabled, FALSE);
    free(todo);

    sprintf(status, "%s%d installed, %d already there, %d failed. Target: %.150s",
            stopped ? "Stopped. " : "Done. ", installed, skipped, failed,
            source == SRC_AMINET ? engine_aminet_target() : engine_extract_target());
    set_status(status);
    alog("%s", status);
}

/* ---- setup ---- */

static Object *build_app(void)
{
    return ApplicationObject,
        MUIA_Application_Title, (ULONG)"WHDSelector",
        MUIA_Application_Version, (ULONG)version,
        MUIA_Application_Copyright, (ULONG)"Personal use",
        MUIA_Application_Description, (ULONG)"Browser for Retroplay WHDLoad packs and Aminet",
        MUIA_Application_Base, (ULONG)"WHDSELECTOR",

        SubWindow, win = WindowObject,
            MUIA_Window_Title, (ULONG)"WHDSelector",
            MUIA_Window_ID, MAKE_ID('W', 'H', 'D', 'S'),
            WindowContents, VGroup,

                Child, HGroup,
                    Child, Label2("Source:"),
                    Child, cy_source = CycleObject,
                        MUIA_Cycle_Entries, (ULONG)source_labels,
                    End,
                    /* MUI 3.8 cycles cannot change their entries, so the
                     * pack and category gadgets are two pages. */
                    Child, grp_pages = PageGroup,
                        Child, HGroup,
                            Child, Label2("Pack:"),
                            Child, cy_pack = CycleObject,
                                MUIA_Cycle_Entries, (ULONG)pack_labels,
                            End,
                        End,
                        Child, HGroup,
                            Child, Label2("Category:"),
                            Child, cy_cat = CycleObject,
                                MUIA_Cycle_Entries, (ULONG)cat_labels,
                            End,
                        End,
                    End,
                    Child, Label2("Letter:"),
                    Child, cy_letter = CycleObject,
                        MUIA_Cycle_Entries, (ULONG)letter_labels,
                    End,
                End,

                Child, HGroup,
                    Child, Label2("Search:"),
                    Child, str_search = StringObject,
                        StringFrame,
                        MUIA_String_MaxLen, 100,
                        MUIA_CycleChain, 1,
                    End,
                End,

                Child, lv_list = NListviewObject,
                    MUIA_CycleChain, 1,
                    MUIA_NListview_NList, lst = NListObject,
                        /* No bars; DELTA gives clear space between columns. */
                        MUIA_NList_Format, (ULONG)FORMAT_RETROPLAY,
                        MUIA_NList_Title, TRUE,
                        MUIA_NList_DisplayHook, (ULONG)&display_hook,
                        MUIA_NList_Input, TRUE,
                        MUIA_NList_MultiSelect, MUIV_NList_MultiSelect_Default,
                    End,
                End,

                Child, HGroup,
                    Child, bt_download = SimpleButton("_Download selected"),
                    Child, bt_stop = SimpleButton("_Stop"),
                    Child, HSpace(0),
                    Child, bt_refresh = SimpleButton("_Refresh list"),
                    Child, bt_settings = SimpleButton("S_ettings..."),
                End,

                Child, gauge = GaugeObject,
                    GaugeFrame,
                    MUIA_Gauge_Horiz, TRUE,
                    MUIA_FixHeightTxt, (ULONG)"X",
                End,

                Child, txt_status = TextObject,
                    TextFrame,
                    MUIA_Background, MUII_TextBack,
                    MUIA_Text_Contents, (ULONG)"",
                End,

                Child, VGroup, GroupFrameT("Log"),
                    MUIA_VertWeight, 35,
                    Child, NListviewObject,
                        MUIA_NListview_NList, lst_log = NListObject,
                            MUIA_NList_Input, FALSE,
                            MUIA_NList_ConstructHook, MUIV_NList_ConstructHook_String,
                            MUIA_NList_DestructHook, MUIV_NList_DestructHook_String,
                        End,
                    End,
                End,
            End,
        End,

        SubWindow, win_set = WindowObject,
            MUIA_Window_Title, (ULONG)"WHDSelector Settings",
            MUIA_Window_ID, MAKE_ID('W', 'H', 'S', 'E'),
            WindowContents, VGroup,

                Child, VGroup, GroupFrameT("Install"),
                    Child, HGroup,
                        Child, Label2("Install directory:"),
                        Child, PopaslObject,
                            MUIA_Popstring_String, str_dir = StringObject,
                                StringFrame,
                                MUIA_String_MaxLen, 255,
                                MUIA_CycleChain, 1,
                            End,
                            MUIA_Popstring_Button, PopButton(MUII_PopDrawer),
                            ASLFR_TitleText, (ULONG)"Select install directory",
                            ASLFR_DrawersOnly, TRUE,
                        End,
                    End,
                    Child, TextObject,
                        MUIA_Text_Contents, (ULONG)"\33cEmpty = whdfetch default (GameFiles in the program directory)",
                    End,
                    Child, HGroup,
                        Child, cm_delete = CheckMark(TRUE),
                        Child, LLabel1("Delete archives after extracting"),
                        Child, HSpace(0),
                    End,
                End,

                Child, VGroup, GroupFrameT("Aminet"),
                    Child, HGroup,
                        Child, Label2("Install directory:"),
                        Child, PopaslObject,
                            MUIA_Popstring_String, str_amidir = StringObject,
                                StringFrame,
                                MUIA_String_MaxLen, 255,
                                MUIA_CycleChain, 1,
                            End,
                            MUIA_Popstring_Button, PopButton(MUII_PopDrawer),
                            ASLFR_TitleText, (ULONG)"Select Aminet install directory",
                            ASLFR_DrawersOnly, TRUE,
                        End,
                    End,
                    Child, TextObject,
                        MUIA_Text_Contents, (ULONG)"\33cEach package goes to its own drawer. Empty = Aminet in the program directory",
                    End,
                End,

                Child, VGroup, GroupFrameT("WHDLoad list filters (applied on Refresh list)"),
                    Child, ColGroup(4),
                        Child, cm_aga = CheckMark(FALSE),
                        Child, LLabel1("Hide AGA"),
                        Child, cm_cd = CheckMark(FALSE),
                        Child, LLabel1("Hide CD32 / CDTV"),
                        Child, cm_ntsc = CheckMark(FALSE),
                        Child, LLabel1("Hide NTSC"),
                        Child, cm_eng = CheckMark(FALSE),
                        Child, LLabel1("Hide non-English"),
                    End,
                End,

                Child, HGroup,
                    Child, bt_save = SimpleButton("_Save"),
                    Child, HSpace(0),
                    Child, bt_cancel = SimpleButton("_Cancel"),
                End,
            End,
        End,
    End;
}

/* ---- settings window ---- */

static void open_settings(void)
{
    engine_settings s;
    engine_get_settings(&s);
    set(str_dir, MUIA_String_Contents, (ULONG)s.install_dir);
    set(str_amidir, MUIA_String_Contents, (ULONG)s.aminet_dir);
    set(cm_delete, MUIA_Selected, s.delete_archives);
    set(cm_aga, MUIA_Selected, s.skip_aga);
    set(cm_cd, MUIA_Selected, s.skip_cd);
    set(cm_ntsc, MUIA_Selected, s.skip_ntsc);
    set(cm_eng, MUIA_Selected, s.skip_non_english);
    set(win_set, MUIA_Window_Open, TRUE);
}

static LONG checked(Object *o)
{
    LONG v = 0;
    get(o, MUIA_Selected, &v);
    return v ? 1 : 0;
}

static void save_settings(void)
{
    static char msg[160];
    engine_settings s;
    STRPTR dir = NULL;

    memset(&s, 0, sizeof(s));
    get(str_dir, MUIA_String_Contents, &dir);
    if (dir) {
        strncpy(s.install_dir, (const char *)dir, sizeof(s.install_dir) - 1);
        /* whdfetch joins paths with '/', so a drawer needs a trailing one. */
        if (s.install_dir[0] && strlen(s.install_dir) < sizeof(s.install_dir) - 1) {
            char last = s.install_dir[strlen(s.install_dir) - 1];
            if (last != ':' && last != '/') strcat(s.install_dir, "/");
        }
    }
    dir = NULL;
    get(str_amidir, MUIA_String_Contents, &dir);
    if (dir) {
        strncpy(s.aminet_dir, (const char *)dir, sizeof(s.aminet_dir) - 2);
        if (s.aminet_dir[0]) {
            char last = s.aminet_dir[strlen(s.aminet_dir) - 1];
            if (last != ':' && last != '/') strcat(s.aminet_dir, "/");
        }
    }
    s.delete_archives = (int)checked(cm_delete);
    s.skip_aga = (int)checked(cm_aga);
    s.skip_cd = (int)checked(cm_cd);
    s.skip_ntsc = (int)checked(cm_ntsc);
    s.skip_non_english = (int)checked(cm_eng);

    msg[0] = 0;
    if (engine_save_settings(&s, msg, sizeof(msg))) {
        sprintf(msg, "Settings saved. Install to: %.120s", engine_extract_target());
        set(win_set, MUIA_Window_Open, FALSE);
    }
    set_status(msg);
}

static void setup_notifies(void)
{
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (ULONG)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(cy_pack, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_PACK);
    DoMethod(cy_cat, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_PACK);
    DoMethod(cy_source, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_SOURCE);
    DoMethod(cy_letter, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_LETTER);
    DoMethod(str_search, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_SEARCH);
    DoMethod(bt_refresh, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_REFRESH);
    DoMethod(bt_download, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_DOWNLOAD);
    DoMethod(bt_stop, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_STOP);
    DoMethod(lst, MUIM_Notify, MUIA_NList_DoubleClick, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_DOWNLOAD);
    DoMethod(bt_settings, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_SETTINGS);
    DoMethod(bt_save, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_SET_SAVE);
    DoMethod(bt_cancel, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_SET_CANCEL);
    DoMethod(win_set, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_SET_CANCEL);
}

int main(void)
{
    ULONG sigs = 0;
    LONG id;
    int rc = 0;
    BPTR old_dir, nil_in = 0, nil_out = 0, old_in = 0, old_out = 0;

    /* whdfetch's paths ("temp/Dat files") are relative to its own
     * directory; run from there whether started from Shell or Workbench. */
    old_dir = CurrentDir(GetProgramDir());
    init_letters();
    init_cat_labels();
    MUIMasterBase = OpenLibrary((STRPTR)MUIMASTER_NAME, 19);
    if (!MUIMasterBase) {
        printf("WHDSelector needs MUI 3.8 or newer (muimaster.library v19).\n");
        CurrentDir(old_dir);
        return RETURN_FAIL;
    }

    app = build_app();
    if (!app) {
        printf("Cannot create the window. Is NList.mcc installed?\n");
        CloseLibrary(MUIMasterBase);
        CurrentDir(old_dir);
        return RETURN_FAIL;
    }
    setup_notifies();
    set(bt_stop, MUIA_Disabled, TRUE);
    set(win, MUIA_Window_Open, TRUE);

    /* whdfetch's code prints progress and runs c:lha, c:unzip and protect
     * through SystemTagList(). Send all of that to NIL:, otherwise
     * AmigaOS opens console windows that stay on screen. */
    nil_in = Open((STRPTR)"NIL:", MODE_OLDFILE);
    nil_out = Open((STRPTR)"NIL:", MODE_NEWFILE);
    if (nil_in && nil_out) {
        old_in = SelectInput(nil_in);
        old_out = SelectOutput(nil_out);
        freopen("NIL:", "w", stdout);
        freopen("NIL:", "w", stderr);
    }

    alog_init(lst_log);
    alog("%s started", version + 6);

    /* Without a network the lists can still be browsed (E-05). */
    alog_capture_begin();
    engine_ok = engine_init(engine_msg, sizeof(engine_msg));
    alog_capture_end();
    if (engine_ok) alog("Network ready. Install target: %s", engine_extract_target());
    else alog("%s", engine_msg);
    load_pack();
    if (games.count > 0) alog("Loaded %s: %ld titles", FilePart((STRPTR)list_file), games.count);
    if (!engine_ok) set_status(engine_msg);

    for (;;) {
        id = (LONG)DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
        if (id == (LONG)MUIV_Application_ReturnID_Quit) break;
        switch (id) {
        case ID_PACK:     load_pack(); break;
        case ID_SOURCE:   switch_source(); break;
        case ID_SEARCH:
        case ID_LETTER:   apply_filter(); break;
        case ID_REFRESH:  refresh_lists(); break;
        case ID_DOWNLOAD: download_selected(); break;
        case ID_SETTINGS: open_settings(); break;
        case ID_SET_SAVE: save_settings(); break;
        case ID_SET_CANCEL: set(win_set, MUIA_Window_Open, FALSE); break;
        }
        if (quit_after_download) break;
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C) break;
        }
    }

    /* Log the shutdown while the log panel still exists. */
    alog_capture_begin();
    engine_shutdown();
    alog_capture_end();
    alog("WHDSelector closed");
    alog_close();

    set(win, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
    free(shown);
    gl_free(&games);
    CloseLibrary(MUIMasterBase);
    if (old_out || old_in) {
        SelectInput(old_in);
        SelectOutput(old_out);
    }
    if (nil_in) Close(nil_in);
    if (nil_out) Close(nil_out);
    CurrentDir(old_dir);
    return rc;
}
