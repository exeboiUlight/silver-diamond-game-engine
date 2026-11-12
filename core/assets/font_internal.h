/* font_internal.h - shared internals of the font loader
 *
 * Not part of the public API: font.c (sfnt/TrueType + rasterizer) and
 * font_cff.c (OpenType/CFF) share the font state and the outline sink.
 */
#ifndef SDGE_ASSETS_FONT_INTERNAL_H
#define SDGE_ASSETS_FONT_INTERNAL_H

#include "font.h"

/* the outline sink: the glyph outlines are flattened into a list of
 * edges that the scanline filler consumes */
typedef struct {
    float *e;                 /* 4 floats per edge: x0,y0,x1,y1 */
    int    n, cap;
    int    open;              /* a contour is open */
    float  cx, cy;            /* the current point */
    float  sx, sy;            /* the start of the contour */
    float  scale;             /* pixels per font unit */
} sdga_pen;

void sdga_pen_init(sdga_pen *p, float scale);
void sdga_pen_free(sdga_pen *p);
void sdga_pen_move(sdga_pen *p, float x, float y);
void sdga_pen_line(sdga_pen *p, float x, float y);
void sdga_pen_quad(sdga_pen *p, float cx, float cy, float x, float y);
void sdga_pen_cubic(sdga_pen *p, float c1x, float c1y,
                    float c2x, float c2y, float x, float y);
void sdga_pen_close(sdga_pen *p);

/* the font state */
struct sdga_font {
    unsigned char *data;      /* the file image */
    size_t         size;

    /* the sfnt tables */
    const unsigned char *glyf;  size_t glyf_len;
    const unsigned char *loca;  size_t loca_len;
    const unsigned char *cmap;  size_t cmap_len;
    const unsigned char *hmtx;  size_t hmtx_len;
    const unsigned char *hhea;  size_t hhea_len;
    const unsigned char *head;  size_t head_len;
    const unsigned char *maxp;  size_t maxp_len;
    const unsigned char *cff;   size_t cff_len;

    int units_per_em;
    int num_glyphs;
    int num_hmetrics;
    int loc_format;           /* 0 = short, 1 = long */
    int ascent, descent, line_gap;   /* in font units */
    int is_cff;

    /* the output */
    unsigned char *pixels;    /* the shared atlas coverage (A8) */
    int    tex_w, tex_h;
    int    shelf_x, shelf_y, shelf_h;   /* the atlas packer state */

    /* one rasterization per used size */
    int    num_faces;
    struct {
        float size;           /* the pixel size of the face */
        int   num_glyphs;
        sdga_glyph *glyphs;   /* [num_glyphs] */
        int   cp_to_glyph[256];  /* the codepoint -> the glyph index */
        int  *rects;          /* [num_glyphs * 4]: x, y, w, h in the atlas */
    } faces[SDGE_FONT_MAX_FACES];
    float  pixel_size;
    float  scale;            /* pixels per font unit */
    char   name[128];
};

/* the OpenType/CFF support (font_cff.c): fills the pen with the outline
 * of the glyph `gid`; returns 0 on success */
int sdga__cff_outline(struct sdga_font *f, int gid, sdga_pen *pen);

#endif /* SDGE_ASSETS_FONT_INTERNAL_H */
