/* HippoPlayer remote control over ARexx (rexxsyslib.library, OS 2.0+). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/ports.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <rexx/storage.h>
#include <rexx/rxslib.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/rexxsyslib.h>

#include "gui/hippo.h"

#define HIPPO_PORT "HIPPOPLAYER"

struct RxsLib *RexxSysBase;

static void set_msg(char *msg, int len, const char *text)
{
    if (msg && len > 0) {
        strncpy(msg, text, (size_t)len - 1);
        msg[len - 1] = 0;
    }
}

/* Sends one command; with result != NULL asks for a result string.
 * Returns 1 if HippoPlayer accepted the command (rm_Result1 == 0). */
static int rexx_cmd(const char *cmd, char *result, int result_len)
{
    struct MsgPort *reply, *port;
    struct RexxMsg *rm;
    int ok = 0;

    reply = CreateMsgPort();
    if (!reply) return 0;
    rm = CreateRexxMsg(reply, NULL, NULL);
    if (!rm) { DeleteMsgPort(reply); return 0; }
    rm->rm_Args[0] = (STRPTR)CreateArgstring((CONST_STRPTR)cmd, strlen(cmd));
    rm->rm_Action = RXCOMM | (result ? RXFF_RESULT : 0);

    if (rm->rm_Args[0]) {
        Forbid();
        port = FindPort((CONST_STRPTR)HIPPO_PORT);
        if (port) PutMsg(port, (struct Message *)rm);
        Permit();
        if (port) {
            WaitPort(reply);
            GetMsg(reply);
            ok = rm->rm_Result1 == 0;
            if (ok && result && rm->rm_Result2) {
                strncpy(result, (const char *)rm->rm_Result2, (size_t)result_len - 1);
                result[result_len - 1] = 0;
            }
            if (result && rm->rm_Result1 == 0 && rm->rm_Result2)
                DeleteArgstring((UBYTE *)rm->rm_Result2);
        }
        DeleteArgstring((UBYTE *)rm->rm_Args[0]);
    }
    DeleteRexxMsg(rm);
    DeleteMsgPort(reply);
    return ok;
}

static int port_exists(void)
{
    struct MsgPort *p;
    Forbid();
    p = FindPort((CONST_STRPTR)HIPPO_PORT);
    Permit();
    return p != NULL;
}

static int start_hippo(const char *player_path)
{
    char cmd[300];
    BPTR in, out;
    int i;

    if (strlen(player_path) > 250) return 0;
    sprintf(cmd, "\"%s\"", player_path);
    in = Open((CONST_STRPTR)"NIL:", MODE_OLDFILE);
    out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    if (!in || !out) {
        if (in) Close(in);
        if (out) Close(out);
        return 0;
    }
    /* Asynchronous: the new process owns and closes the handles. */
    if (SystemTags((CONST_STRPTR)cmd, SYS_Input, (ULONG)in, SYS_Output, (ULONG)out,
                   SYS_Asynch, TRUE, NP_StackSize, 16384, TAG_DONE) == -1) {
        Close(in);
        Close(out);
        return 0;
    }
    for (i = 0; i < 75 && !port_exists(); i++) Delay(10);   /* up to 15 s */
    return port_exists();
}

int hippo_play(const char *file, const char *player_path, char *msg, int msg_len)
{
    char cmd[400], result[32];
    long count;
    int ok;

    if (strchr(file, ' ') || strlen(file) > 380) {
        set_msg(msg, msg_len, "Path contains spaces or is too long for HippoPlayer.");
        return 0;
    }
    RexxSysBase = (struct RxsLib *)OpenLibrary((CONST_STRPTR)"rexxsyslib.library", 36);
    if (!RexxSysBase) {
        set_msg(msg, msg_len, "rexxsyslib.library not available (ARexx is needed).");
        return 0;
    }

    if (!port_exists()) {
        if (!player_path || !player_path[0]) {
            set_msg(msg, msg_len, "HippoPlayer is not running. Start it or set its path in Settings.");
            CloseLibrary((struct Library *)RexxSysBase);
            return 0;
        }
        if (!start_hippo(player_path)) {
            set_msg(msg, msg_len, "Could not start HippoPlayer (check its path in Settings).");
            CloseLibrary((struct Library *)RexxSysBase);
            return 0;
        }
    }

    /* Same sequence as PlayMod: append to the list, select it, play. */
    result[0] = 0;
    count = rexx_cmd("GET NFIL", result, sizeof(result)) ? strtol(result, 0, 10) : 0;
    sprintf(cmd, "ADD %s", file);
    ok = rexx_cmd(cmd, NULL, 0);
    if (ok) {
        sprintf(cmd, "CHOOSE %ld", count + 1);
        ok = rexx_cmd(cmd, NULL, 0) && rexx_cmd("PLAY", NULL, 0);
    }
    if (!ok) set_msg(msg, msg_len, "HippoPlayer did not accept the ARexx commands.");
    CloseLibrary((struct Library *)RexxSysBase);
    RexxSysBase = NULL;
    return ok;
}
