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
#include <mui/NListview_mcc.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <clib/alib_protos.h>

#include "gui/game_list.h"
#include "gamefile_parser.h"

extern const char *WHDLOAD_FILE_FILTER_DAT_GAMES;
extern const char *WHDLOAD_FILE_FILTER_DAT_GAMES_BETA_AND_UNRELEASED;
extern const char *WHDLOAD_FILE_FILTER_DAT_DEMOS;
extern const char *WHDLOAD_FILE_FILTER_DAT_BETA_AND_UNRELEASED;
extern const char *WHDLOAD_FILE_FILTER_DAT_MAGAZINES;
extern const char *DIR_DAT_FILES;

size_t __stack = 65536;
static const char version[] = "$VER: WHDSelector 0.2 (19.10.2026)";

struct Library *MUIMasterBase;

#define MAKE_ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))

enum { ID_PACK = 1, ID_SEARCH, ID_LETTER, ID_REFRESH };

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
static gl_list games;
static gl_entry **shown;
static char list_file[256];

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
    get(cy_pack, MUIA_Cycle_Active, &pack);

    free(shown);
    shown = NULL;
    gl_free(&games);
    list_file[0] = 0;

    if (find_list_file(pack_filter((int)pack), list_file, sizeof(list_file)) &&
        gl_load(&games, list_file)) {
        shown = (gl_entry **)malloc((size_t)(games.count + 1) * sizeof(gl_entry *));
        if (!shown) gl_free(&games);
    }
    apply_filter();
}

/* Runs whdfetch from PROGDIR: to download the latest DATs (no games). */
static void refresh_lists(void)
{
    BPTR con, old_dir, prog_dir;
    LONG rc;

    set(app, MUIA_Application_Sleep, TRUE);
    set_status("Fetching the latest lists with whdfetch...");

    prog_dir = Lock((STRPTR)"PROGDIR:", ACCESS_READ);
    con = Open((STRPTR)"CON:20/20/600/180/whdfetch/AUTO/CLOSE/WAIT", MODE_NEWFILE);
    if (!prog_dir || !con) {
        if (con) Close(con);
        if (prog_dir) UnLock(prog_dir);
        set(app, MUIA_Application_Sleep, FALSE);
        set_status("Could not start whdfetch.");
        return;
    }
    old_dir = CurrentDir(prog_dir);
    rc = SystemTags((STRPTR)"whdfetch DOWNLOADALL ESTIMATESPACE",
                    SYS_Input, (ULONG)con, SYS_Output, (ULONG)NULL, TAG_DONE);
    CurrentDir(old_dir);
    UnLock(prog_dir);
    Close(con);
    set(app, MUIA_Application_Sleep, FALSE);

    if (rc != 0) {
        static char msg[80];
        sprintf(msg, "whdfetch failed (return code %ld).", (long)rc);
        set_status(msg);
        return;
    }
    load_pack();
}

/* ---- setup ---- */

static Object *build_app(void)
{
    return ApplicationObject,
        MUIA_Application_Title, (ULONG)"WHDSelector",
        MUIA_Application_Version, (ULONG)version,
        MUIA_Application_Copyright, (ULONG)"Personal use",
        MUIA_Application_Description, (ULONG)"Browser for Retroplay WHDLoad packs",
        MUIA_Application_Base, (ULONG)"WHDSELECTOR",

        SubWindow, win = WindowObject,
            MUIA_Window_Title, (ULONG)"WHDSelector",
            MUIA_Window_ID, MAKE_ID('W', 'H', 'D', 'S'),
            WindowContents, VGroup,

                Child, HGroup,
                    Child, Label2("Pack:"),
                    Child, cy_pack = CycleObject,
                        MUIA_Cycle_Entries, (ULONG)pack_labels,
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
                        MUIA_NList_Format, (ULONG)"BAR,BAR,BAR,P=\33r",
                        MUIA_NList_Title, TRUE,
                        MUIA_NList_DisplayHook, (ULONG)&display_hook,
                        MUIA_NList_Input, TRUE,
                    End,
                End,

                Child, HGroup,
                    Child, bt_refresh = SimpleButton("_Refresh list"),
                    Child, HSpace(0),
                End,

                Child, txt_status = TextObject,
                    TextFrame,
                    MUIA_Background, MUII_TextBack,
                    MUIA_Text_Contents, (ULONG)"",
                End,
            End,
        End,
    End;
}

static void setup_notifies(void)
{
    DoMethod(win, MUIM_Notify, MUIA_Window_CloseRequest, TRUE,
             (ULONG)app, 2, MUIM_Application_ReturnID, MUIV_Application_ReturnID_Quit);
    DoMethod(cy_pack, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_PACK);
    DoMethod(cy_letter, MUIM_Notify, MUIA_Cycle_Active, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_LETTER);
    DoMethod(str_search, MUIM_Notify, MUIA_String_Contents, MUIV_EveryTime,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_SEARCH);
    DoMethod(bt_refresh, MUIM_Notify, MUIA_Pressed, FALSE,
             (ULONG)app, 2, MUIM_Application_ReturnID, ID_REFRESH);
}

int main(void)
{
    ULONG sigs = 0;
    LONG id;
    int rc = 0;
    BPTR old_dir;

    /* whdfetch's paths ("temp/Dat files") are relative to its own
     * directory; run from there whether started from Shell or Workbench. */
    old_dir = CurrentDir(GetProgramDir());
    init_letters();
    MUIMasterBase = OpenLibrary((STRPTR)MUIMASTER_NAME, 19);
    if (!MUIMasterBase) {
        printf("WHDSelector needs MUI 3.8 or newer (muimaster.library v19).\n");
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
    set(win, MUIA_Window_Open, TRUE);
    load_pack();

    for (;;) {
        id = (LONG)DoMethod(app, MUIM_Application_NewInput, (ULONG)&sigs);
        if (id == (LONG)MUIV_Application_ReturnID_Quit) break;
        switch (id) {
        case ID_PACK:    load_pack(); break;
        case ID_SEARCH:
        case ID_LETTER:  apply_filter(); break;
        case ID_REFRESH: refresh_lists(); break;
        }
        if (sigs) {
            sigs = Wait(sigs | SIGBREAKF_CTRL_C);
            if (sigs & SIGBREAKF_CTRL_C) break;
        }
    }

    set(win, MUIA_Window_Open, FALSE);
    MUI_DisposeObject(app);
    free(shown);
    gl_free(&games);
    CloseLibrary(MUIMasterBase);
    CurrentDir(old_dir);
    return rc;
}
