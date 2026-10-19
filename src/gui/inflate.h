#ifndef GUI_INFLATE_H
#define GUI_INFLATE_H

/* Result codes. */
#define IF_OK            0
#define IF_ERR_DATA      1   /* corrupt data or CRC mismatch */
#define IF_ERR_IO        2
#define IF_ERR_FORMAT    3   /* not a gzip file */
#define IF_ERR_NOMEM     4
#define IF_ERR_CANCELLED 5

/* Optional: polled during long decodes; nonzero cancels. */
extern int (*inflate_cancel_hook)(void);

/* Decompresses a .gz file to out_path (written as out_path".tmp" first,
 * renamed on success). Streams through a 32 KB window; CRC is checked. */
int inflate_gunzip_file(const char *gz_path, const char *out_path);

unsigned long inflate_crc32(unsigned long crc, const unsigned char *buf, long len);

#endif
