#ifndef GUI_LOGO_DATA_H
#define GUI_LOGO_DATA_H

#include <exec/types.h>

/* WHDSelector logo (tools/make_logo.py): uncompressed ILBM BODY,
 * interleaved bitplanes, and 32-bit-per-gun RGB palette for
 * MUIA_Bitmap_SourceColors. */
#define LOGO_WIDTH  400
#define LOGO_HEIGHT 64
#define LOGO_DEPTH  4

extern const ULONG logo_colors[3 << LOGO_DEPTH];
extern const UBYTE logo_body[];

#endif
