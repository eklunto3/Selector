/* Own deflate decoder (RFC 1951) with a gzip (RFC 1952) front end, used
 * for Aminet INDEX.gz. Portable C only. Canonical Huffman decoding,
 * 32 KB sliding window flushed to the output file whenever it wraps. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gui/inflate.h"

#define WIN_SIZE  32768U
#define IN_SIZE   4096
#define MAXBITS   15
#define MAXLCODES 286
#define MAXDCODES 30
#define FIXLCODES 288

typedef struct {
    short count[MAXBITS + 1];
    short symbol[FIXLCODES];
} huffman;

typedef struct {
    FILE *in;
    unsigned char inbuf[IN_SIZE];
    int inpos, inlen;
    unsigned long remaining;   /* compressed bytes still unread from file */
    unsigned long bitbuf;
    int bitcnt;
    int err;                   /* nonzero: input ran out or bad data */

    FILE *out;
    inflate_sink_fn sink;      /* when set, output goes here instead of out */
    void *sink_user;
    unsigned char win[WIN_SIZE];
    unsigned wpos;
    unsigned long total;       /* bytes produced */
    unsigned long crc;

    huffman lencode, distcode;
    short lengths[MAXLCODES + MAXDCODES];
} inflater;

static unsigned long crc_table[256];
static int crc_ready;

unsigned long inflate_crc32(unsigned long crc, const unsigned char *buf, long len)
{
    if (!crc_ready) {
        unsigned long c;
        int n, k;
        for (n = 0; n < 256; n++) {
            c = (unsigned long)n;
            for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
            crc_table[n] = c;
        }
        crc_ready = 1;
    }
    crc = ~crc & 0xFFFFFFFFUL;
    while (len-- > 0) crc = crc_table[(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
    return ~crc & 0xFFFFFFFFUL;
}

static int get_byte(inflater *s)
{
    if (s->inpos == s->inlen) {
        size_t want = IN_SIZE;
        if (s->remaining == 0) { s->err = 1; return 0; }
        if (want > s->remaining) want = s->remaining;
        s->inlen = (int)fread(s->inbuf, 1, want, s->in);
        s->inpos = 0;
        if (s->inlen <= 0) { s->inlen = 0; s->err = 1; return 0; }
        s->remaining -= (unsigned long)s->inlen;
    }
    return s->inbuf[s->inpos++];
}

static unsigned bits(inflater *s, int need)
{
    unsigned long val = s->bitbuf;
    while (s->bitcnt < need) {
        if (s->err) return 0;
        val |= (unsigned long)get_byte(s) << s->bitcnt;
        s->bitcnt += 8;
    }
    s->bitbuf = val >> need;
    s->bitcnt -= need;
    return (unsigned)(val & ((1UL << need) - 1));
}

static int flush_window(inflater *s, unsigned n)
{
    s->crc = inflate_crc32(s->crc, s->win, (long)n);
    if (s->sink) return s->sink(s->win, n, s->sink_user);
    if (fwrite(s->win, 1, n, s->out) != n) return 0;
    return 1;
}

/* Returns 0 on write error. */
static int put_byte(inflater *s, unsigned char c)
{
    s->win[s->wpos++] = c;
    s->total++;
    if (s->wpos == WIN_SIZE) {
        if (!flush_window(s, WIN_SIZE)) return 0;
        s->wpos = 0;
    }
    return 1;
}

/* Builds a canonical Huffman decoding table. Returns 0 for a complete
 * code, >0 for an incomplete one, <0 for an over-subscribed one. */
static int construct(huffman *h, const short *length, int n)
{
    int symbol, len, left;
    short offs[MAXBITS + 1];

    for (len = 0; len <= MAXBITS; len++) h->count[len] = 0;
    for (symbol = 0; symbol < n; symbol++) h->count[length[symbol]]++;
    if (h->count[0] == n) return 0;

    left = 1;
    for (len = 1; len <= MAXBITS; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return left;
    }
    offs[1] = 0;
    for (len = 1; len < MAXBITS; len++) offs[len + 1] = offs[len] + h->count[len];
    for (symbol = 0; symbol < n; symbol++)
        if (length[symbol] != 0) h->symbol[offs[length[symbol]]++] = (short)symbol;
    return left;
}

static int decode(inflater *s, const huffman *h)
{
    int code = 0, first = 0, index = 0, len, count;
    for (len = 1; len <= MAXBITS; len++) {
        code |= (int)bits(s, 1);
        if (s->err) return -1;
        count = h->count[len];
        if (code - count < first) return h->symbol[index + (code - first)];
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    return -1;
}

static const short lbase[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short lext[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dbase[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
    8193, 12289, 16385, 24577 };
static const short dext[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static int codes(inflater *s)
{
    int symbol;
    unsigned len, dist;
    unsigned long poll = 0;

    for (;;) {
        symbol = decode(s, &s->lencode);
        if (symbol < 0) return IF_ERR_DATA;
        if (symbol < 256) {
            if (!put_byte(s, (unsigned char)symbol)) return IF_ERR_IO;
        } else if (symbol == 256) {
            return IF_OK;
        } else {
            symbol -= 257;
            if (symbol >= 29) return IF_ERR_DATA;
            len = (unsigned)lbase[symbol] + bits(s, lext[symbol]);
            symbol = decode(s, &s->distcode);
            if (symbol < 0 || symbol >= 30) return IF_ERR_DATA;
            dist = (unsigned)dbase[symbol] + bits(s, dext[symbol]);
            if (s->err) return IF_ERR_DATA;
            if (dist > s->total) return IF_ERR_DATA;
            while (len--) {
                unsigned char c = s->win[(s->wpos - dist) & (WIN_SIZE - 1)];
                if (!put_byte(s, c)) return IF_ERR_IO;
            }
        }
        if ((++poll & 0x3FFF) == 0 && (inflate_cancel_hook && inflate_cancel_hook())) return IF_ERR_CANCELLED;
    }
}

static int stored(inflater *s)
{
    unsigned len, nlen;
    s->bitbuf = 0;   /* discard bits up to the next byte boundary */
    s->bitcnt = 0;
    len = (unsigned)get_byte(s);
    len |= (unsigned)get_byte(s) << 8;
    nlen = (unsigned)get_byte(s);
    nlen |= (unsigned)get_byte(s) << 8;
    if (s->err || len != (~nlen & 0xFFFF)) return IF_ERR_DATA;
    while (len--) {
        unsigned char c = (unsigned char)get_byte(s);
        if (s->err) return IF_ERR_DATA;
        if (!put_byte(s, c)) return IF_ERR_IO;
    }
    return IF_OK;
}

static int fixed(inflater *s)
{
    int i;
    for (i = 0; i < 144; i++) s->lengths[i] = 8;
    for (; i < 256; i++) s->lengths[i] = 9;
    for (; i < 280; i++) s->lengths[i] = 7;
    for (; i < FIXLCODES; i++) s->lengths[i] = 8;
    construct(&s->lencode, s->lengths, FIXLCODES);
    for (i = 0; i < MAXDCODES; i++) s->lengths[i] = 5;
    construct(&s->distcode, s->lengths, MAXDCODES);
    return codes(s);
}

static int dynamic(inflater *s)
{
    static const short order[19] =
        { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    int nlen, ndist, ncode, index, err;

    nlen = (int)bits(s, 5) + 257;
    ndist = (int)bits(s, 5) + 1;
    ncode = (int)bits(s, 4) + 4;
    if (s->err || nlen > MAXLCODES || ndist > MAXDCODES) return IF_ERR_DATA;

    for (index = 0; index < ncode; index++) s->lengths[order[index]] = (short)bits(s, 3);
    for (; index < 19; index++) s->lengths[order[index]] = 0;
    if (construct(&s->lencode, s->lengths, 19) != 0) return IF_ERR_DATA;

    index = 0;
    while (index < nlen + ndist) {
        int symbol = decode(s, &s->lencode);
        int len = 0, rep;
        if (symbol < 0) return IF_ERR_DATA;
        if (symbol < 16) {
            s->lengths[index++] = (short)symbol;
            continue;
        }
        if (symbol == 16) {
            if (index == 0) return IF_ERR_DATA;
            len = s->lengths[index - 1];
            rep = 3 + (int)bits(s, 2);
        } else if (symbol == 17) {
            rep = 3 + (int)bits(s, 3);
        } else {
            rep = 11 + (int)bits(s, 7);
        }
        if (s->err || index + rep > nlen + ndist) return IF_ERR_DATA;
        while (rep--) s->lengths[index++] = (short)len;
    }
    if (s->lengths[256] == 0) return IF_ERR_DATA;

    err = construct(&s->lencode, s->lengths, nlen);
    if (err < 0 || (err > 0 && nlen - s->lencode.count[0] != 1)) return IF_ERR_DATA;
    err = construct(&s->distcode, s->lengths + nlen, ndist);
    if (err < 0 || (err > 0 && ndist - s->distcode.count[0] != 1)) return IF_ERR_DATA;
    return codes(s);
}

static int inflate_stream(inflater *s)
{
    int last, type;
    int r;
    do {
        last = (int)bits(s, 1);
        type = (int)bits(s, 2);
        if (s->err) return IF_ERR_DATA;
        if (type == 0) r = stored(s);
        else if (type == 1) r = fixed(s);
        else if (type == 2) r = dynamic(s);
        else r = IF_ERR_DATA;
        if (r != IF_OK) return r;
    } while (!last);
    if (s->wpos && !flush_window(s, s->wpos)) return IF_ERR_IO;
    return IF_OK;
}

static unsigned long le16(const unsigned char *p) { return (unsigned long)p[0] | ((unsigned long)p[1] << 8); }
static unsigned long le32(const unsigned char *p) { return le16(p) | (le16(p + 2) << 16); }

int (*inflate_cancel_hook)(void) = 0;

int inflate_gunzip_file(const char *gz_path, const char *out_path)
{
    unsigned char hdr[10], trailer[8];
    unsigned long flags, total_size, crc, isize;
    long data_start;
    char tmp_path[300];
    inflater *s = 0;
    FILE *in = 0, *out = 0;
    int r = IF_OK;

    if (strlen(out_path) + 5 > sizeof(tmp_path)) return IF_ERR_IO;
    strcpy(tmp_path, out_path);
    strcat(tmp_path, ".tmp");

    in = fopen(gz_path, "rb");
    if (!in) return IF_ERR_IO;
    fseek(in, 0, SEEK_END);
    total_size = (unsigned long)ftell(in);
    fseek(in, 0, SEEK_SET);

    if (total_size < 18 || fread(hdr, 1, 10, in) != 10 ||
        hdr[0] != 0x1f || hdr[1] != 0x8b || hdr[2] != 8) { r = IF_ERR_FORMAT; goto done; }
    flags = hdr[3];
    if (flags & 4) {                       /* FEXTRA */
        unsigned char x[2];
        if (fread(x, 1, 2, in) != 2 || fseek(in, (long)le16(x), SEEK_CUR) != 0) { r = IF_ERR_FORMAT; goto done; }
    }
    if (flags & 8) { int c; while ((c = fgetc(in)) != EOF && c != 0) ; }   /* FNAME */
    if (flags & 16) { int c; while ((c = fgetc(in)) != EOF && c != 0) ; }  /* FCOMMENT */
    if (flags & 2) fseek(in, 2, SEEK_CUR);                                 /* FHCRC */
    data_start = ftell(in);
    if (data_start < 0 || (unsigned long)data_start + 8 > total_size) { r = IF_ERR_FORMAT; goto done; }

    s = (inflater *)malloc(sizeof(inflater));
    if (!s) { r = IF_ERR_NOMEM; goto done; }
    memset(s, 0, sizeof(*s));
    s->in = in;
    s->remaining = total_size - (unsigned long)data_start - 8;

    out = fopen(tmp_path, "wb");
    if (!out) { r = IF_ERR_IO; goto done; }
    s->out = out;

    r = inflate_stream(s);
    if (r == IF_OK) {
        /* CRC-32 and size (mod 2^32) of the original data. */
        if (fseek(in, (long)(total_size - 8), SEEK_SET) != 0 || fread(trailer, 1, 8, in) != 8) {
            r = IF_ERR_FORMAT;
        } else {
            crc = le32(trailer);
            isize = le32(trailer + 4);
            if (s->crc != crc || (s->total & 0xFFFFFFFFUL) != isize) r = IF_ERR_DATA;
        }
    }

done:
    if (out && fclose(out) != 0 && r == IF_OK) r = IF_ERR_IO;
    if (in) fclose(in);
    free(s);
    if (r == IF_OK) {
        remove(out_path);
        if (rename(tmp_path, out_path) != 0) r = IF_ERR_IO;
    }
    if (r != IF_OK && out) remove(tmp_path);
    return r;
}

int inflate_unzip_first_to_sink(const char *zip_path, inflate_sink_fn sink, void *user)
{
    unsigned char hdr[30];
    unsigned long flags, method, crc, csize, usize, nlen, xlen;
    inflater *s = 0;
    FILE *in;
    int r = IF_OK;

    in = fopen(zip_path, "rb");
    if (!in) return IF_ERR_IO;
    if (fread(hdr, 1, 30, in) != 30 || le32(hdr) != 0x04034B50UL) { r = IF_ERR_FORMAT; goto done; }
    flags = le16(hdr + 6);
    method = le16(hdr + 8);
    crc = le32(hdr + 14);
    csize = le32(hdr + 18);
    usize = le32(hdr + 22);
    nlen = le16(hdr + 26);
    xlen = le16(hdr + 28);
    /* Encrypted entries and trailing data descriptors are not used by the
     * index files we read; refuse them instead of guessing. */
    if ((flags & 1) || (flags & 8) || method != 8) { r = IF_ERR_FORMAT; goto done; }
    if (fseek(in, (long)(nlen + xlen), SEEK_CUR) != 0) { r = IF_ERR_FORMAT; goto done; }

    s = (inflater *)malloc(sizeof(inflater));
    if (!s) { r = IF_ERR_NOMEM; goto done; }
    memset(s, 0, sizeof(*s));
    s->in = in;
    s->remaining = csize;
    s->sink = sink;
    s->sink_user = user;

    r = inflate_stream(s);
    if (r == IF_OK && (s->total != usize || s->crc != crc)) r = IF_ERR_DATA;

done:
    fclose(in);
    free(s);
    return r;
}
