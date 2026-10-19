#ifndef GUI_CLIP_H
#define GUI_CLIP_H

/* Text on the clipboard (clipboard.device unit 0, IFF FTXT/CHRS),
 * written and read directly without iffparse.library. */

/* Returns 1 on success. */
int clip_write_text(const char *text);

/* Reads the first CHRS chunk into buf (NUL-terminated, cut at buf_len).
 * Returns the number of characters, or -1 if there is no text. */
int clip_read_text(char *buf, int buf_len);

#endif
