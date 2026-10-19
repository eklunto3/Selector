/* Activity log for the GUI: log panel + PROGDIR:WHDSelector.log, and
 * capture of whdfetch's console output. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <exec/types.h>
#include <dos/dos.h>
#include <libraries/mui.h>
#include <mui/NList_mcc.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <clib/alib_protos.h>

#include "gui/activity_log.h"

#define LOG_FILE      "PROGDIR:WHDSelector.log"
#define LOG_FILE_OLD  "PROGDIR:WHDSelector.log.old"
#define LOG_MAX_BYTES (256L * 1024L)
#define PANEL_MAX     1000

/* Two files: vbcc's stdout and the DOS Output() handle cannot share one
 * file (MODE_NEWFILE is exclusive). */
#define CAP_STDOUT    "T:WHDSelector.stdout"
#define CAP_OUTPUT    "T:WHDSelector.output"

static Object *panel;
static FILE *logfile;
static BPTR cap_handle, saved_output;
static int capturing;

void alog_init(Object *nlist)
{
    FILE *f;
    long size = 0;

    panel = nlist;
    f = fopen(LOG_FILE, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        size = ftell(f);
        fclose(f);
    }
    if (size > LOG_MAX_BYTES) {
        DeleteFile((STRPTR)LOG_FILE_OLD);
        Rename((STRPTR)LOG_FILE, (STRPTR)LOG_FILE_OLD);
    }
    logfile = fopen(LOG_FILE, "a");
}

void alog_close(void)
{
    if (capturing) alog_capture_end();
    if (logfile) fclose(logfile);
    logfile = NULL;
    panel = NULL;
}

static void add_line(const char *text)
{
    char line[300];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    LONG entries = 0;

    if (tm) sprintf(line, "%02d:%02d:%02d  ", tm->tm_hour, tm->tm_min, tm->tm_sec);
    else strcpy(line, "--:--:--  ");
    strncat(line, text, sizeof(line) - strlen(line) - 1);

    if (logfile) {
        if (tm)
            fprintf(logfile, "%04d-%02d-%02d %s\n", tm->tm_year + 1900, tm->tm_mon + 1,
                    tm->tm_mday, line);
        else
            fprintf(logfile, "%s\n", line);
        fflush(logfile);
    }
    if (panel) {
        get(panel, MUIA_NList_Entries, &entries);
        if (entries >= PANEL_MAX) DoMethod(panel, MUIM_NList_Remove, MUIV_NList_Remove_First);
        DoMethod(panel, MUIM_NList_InsertSingle, (ULONG)line, MUIV_NList_Insert_Bottom);
        DoMethod(panel, MUIM_NList_Jump, MUIV_NList_Jump_Bottom);
    }
}

void alog(const char *fmt, ...)
{
    char text[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    add_line(text);
}

/* ---- capture ---- */

void alog_capture_begin(void)
{
    if (capturing) return;
    fflush(stdout);
    if (!freopen(CAP_STDOUT, "w", stdout)) return;
    cap_handle = Open((STRPTR)CAP_OUTPUT, MODE_NEWFILE);
    if (cap_handle) saved_output = SelectOutput(cap_handle);
    capturing = 1;
}

/* Removes ESC[...x and CSI(0x9b)...x sequences; of text split by
 * carriage returns (progress bars redraw with \r) keeps the last
 * non-empty part. */
static void clean_line(char *s)
{
    char *r = s, *w = s, *cr;
    /* drop trailing \r so "text\r" keeps "text" */
    size_t n = strlen(s);
    while (n > 0 && s[n - 1] == '\r') s[--n] = 0;
    cr = strrchr(s, '\r');
    if (cr) r = cr + 1;
    while (*r) {
        unsigned char c = (unsigned char)*r;
        if (c == 0x1b && r[1] == '[') {
            r += 2;
            while (*r && !((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z'))) r++;
            if (*r) r++;
            continue;
        }
        if (c == 0x9b) {
            r++;
            while (*r && !((*r >= 'A' && *r <= 'Z') || (*r >= 'a' && *r <= 'z'))) r++;
            if (*r) r++;
            continue;
        }
        if (c < 32 && c != '\t') { r++; continue; }
        *w++ = *r++;
    }
    *w = 0;
    /* trim */
    while (w > s && (w[-1] == ' ' || w[-1] == '\t')) *--w = 0;
}

static void copy_file_to_log(const char *path)
{
    char buf[512];
    FILE *f = fopen(path, "r");
    if (!f) return;
    while (fgets(buf, sizeof(buf), f)) {
        size_t n = strlen(buf);
        if (n && buf[n - 1] == '\n') buf[n - 1] = 0;
        clean_line(buf);
        if (buf[0]) add_line(buf);
    }
    fclose(f);
}

void alog_capture_end(void)
{
    if (!capturing) return;
    fflush(stdout);
    freopen("NIL:", "w", stdout);
    if (cap_handle) {
        SelectOutput(saved_output);
        Close(cap_handle);
        cap_handle = 0;
    }
    capturing = 0;
    copy_file_to_log(CAP_STDOUT);
    copy_file_to_log(CAP_OUTPUT);
    DeleteFile((STRPTR)CAP_STDOUT);
    DeleteFile((STRPTR)CAP_OUTPUT);
}
