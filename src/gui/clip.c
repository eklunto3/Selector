/* Clipboard text via clipboard.device (FTXT), OS 2.0+. */
#include <string.h>

#include <exec/types.h>
#include <exec/io.h>
#include <devices/clipboard.h>
#include <proto/exec.h>

#include "gui/clip.h"

#define ID(a, b, c, d) ((ULONG)(a) << 24 | (ULONG)(b) << 16 | (ULONG)(c) << 8 | (ULONG)(d))

typedef struct {
    struct MsgPort *port;
    struct IOClipReq *io;
} clip_handle;

static int clip_open(clip_handle *h)
{
    h->io = NULL;
    h->port = CreateMsgPort();
    if (!h->port) return 0;
    h->io = (struct IOClipReq *)CreateIORequest(h->port, sizeof(struct IOClipReq));
    if (!h->io) { DeleteMsgPort(h->port); return 0; }
    if (OpenDevice((CONST_STRPTR)"clipboard.device", 0, (struct IORequest *)h->io, 0) != 0) {
        DeleteIORequest((struct IORequest *)h->io);
        DeleteMsgPort(h->port);
        return 0;
    }
    return 1;
}

static void clip_close(clip_handle *h)
{
    CloseDevice((struct IORequest *)h->io);
    DeleteIORequest((struct IORequest *)h->io);
    DeleteMsgPort(h->port);
}

static int clip_write(clip_handle *h, const void *data, LONG len)
{
    h->io->io_Command = CMD_WRITE;
    h->io->io_Data = (STRPTR)data;
    h->io->io_Length = len;
    DoIO((struct IORequest *)h->io);
    return h->io->io_Error == 0;
}

int clip_write_text(const char *text)
{
    clip_handle h;
    ULONG len = (ULONG)strlen(text);
    ULONG pad = len & 1;
    ULONG hdr[5];
    UBYTE zero = 0;
    int ok;

    if (!clip_open(&h)) return 0;
    hdr[0] = ID('F', 'O', 'R', 'M');
    hdr[1] = 4 + 8 + len + pad;           /* "FTXT" + CHRS header + data */
    hdr[2] = ID('F', 'T', 'X', 'T');
    hdr[3] = ID('C', 'H', 'R', 'S');
    hdr[4] = len;

    h.io->io_Offset = 0;
    h.io->io_ClipID = 0;
    ok = clip_write(&h, hdr, sizeof(hdr)) && clip_write(&h, text, (LONG)len) &&
         (!pad || clip_write(&h, &zero, 1));

    h.io->io_Command = CMD_UPDATE;        /* tells the device we are done */
    DoIO((struct IORequest *)h.io);
    clip_close(&h);
    return ok;
}

static LONG clip_read(clip_handle *h, void *buf, LONG len)
{
    h->io->io_Command = CMD_READ;
    h->io->io_Data = (STRPTR)buf;
    h->io->io_Length = len;
    DoIO((struct IORequest *)h->io);
    return h->io->io_Error ? -1 : h->io->io_Actual;
}

int clip_read_text(char *buf, int buf_len)
{
    clip_handle h;
    ULONG hdr[3], chunk[2];
    char skip[64];
    int result = -1;

    if (buf_len <= 0 || !clip_open(&h)) return -1;
    buf[0] = 0;
    h.io->io_Offset = 0;
    h.io->io_ClipID = 0;

    if (clip_read(&h, hdr, 12) == 12 && hdr[0] == ID('F', 'O', 'R', 'M') &&
        hdr[2] == ID('F', 'T', 'X', 'T')) {
        while (clip_read(&h, chunk, 8) == 8) {
            ULONG size = chunk[1], want;
            if (chunk[0] == ID('C', 'H', 'R', 'S')) {
                want = size < (ULONG)(buf_len - 1) ? size : (ULONG)(buf_len - 1);
                if (clip_read(&h, buf, (LONG)want) == (LONG)want) {
                    buf[want] = 0;
                    result = (int)want;
                }
                break;
            }
            /* skip other chunks (padded to even size) */
            size += size & 1;
            while (size > 0) {
                LONG n = clip_read(&h, skip, size > sizeof(skip) ? (LONG)sizeof(skip) : (LONG)size);
                if (n <= 0) { size = 0; break; }
                size -= (ULONG)n;
            }
        }
    }
    /* The device needs the read finished to the end. */
    while (clip_read(&h, skip, sizeof(skip)) > 0)
        ;
    clip_close(&h);
    return result;
}
