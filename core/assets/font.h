/* font.h - Silver Diamond Game Engine :: font loading and rasterizing
 *
 * Two kinds of fonts:
 *   - the built-in 5x7 bitmap font (always available, no file needed);
 *   - TrueType (glyf/loca) and OpenType/CFF (CFF ) files, loaded from
 *     disk and rasterized at load time.
 *
 * The loader produces an atlas image (8 bit coverage) plus the metrics of
 * every glyph. The UI renderer uploads the atlas and draws the text with
 * it - see sdui_set_font() in core/graphics/ui.h.
 *
 *   sdga_font *f = sdga_load_font("assets/font.ttf", 18.0f);
 *   sdui_set_font(f);
 */
#ifndef SDGE_ASSETS_FONT_H
#define SDGE_ASSETS_FONT_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One rasterized glyph in the atlas. */
typedef struct {
    int   w, h;               /* the bitmap size (0 = an empty glyph) */
    int   bearing_x;          /* the offset from the pen, in pixels */
    int   bearing_y;          /* the distance from the baseline up, in pixels */
    float advance;            /* the advance in pixels */
    float u0, v0, u1, v1;     /* the atlas uv */
} sdga_glyph;

typedef struct sdga_font sdga_font;

/* loads a TTF/OTF file and rasterizes the glyphs for the codepoints
 * 32..255 at `pixel_size` (the cap height in pixels).
 * NULL on failure. */
SDGE_API sdga_font *sdga_load_font(const char *path, float pixel_size);

/* the built-in 5x7 bitmap font (no file, always available) */
SDGE_API sdga_font *sdga_load_builtin_font(float pixel_size);

SDGE_API void sdga_free_font(sdga_font *f);

/* the glyph index of a codepoint (0 = .notdef) */
SDGE_API int sdga_font_glyph_index(const sdga_font *f, unsigned codepoint);

/* the glyph data (NULL when the index is out of range) */
SDGE_API const sdga_glyph *sdga_font_glyph(const sdga_font *f, int glyph_id);

SDGE_API int   sdga_font_glyph_count(const sdga_font *f);

/* --------------------------------------------------------- the faces ---
 * A font holds one rasterization per used size ("face"): the glyphs of
 * every face live in ONE atlas, so the UI still draws everything in a
 * single draw call. The UI picks the face that matches the requested size
 * the closest - that way the bitmaps are never scaled down and the thin
 * strokes stay readable.
 */
#define SDGE_FONT_MAX_FACES 8

/* adds a rasterization at `pixel_size` (the atlas grows); 0 on success */
SDGE_API int sdga_font_add_face(sdga_font *f, float pixel_size);
SDGE_API int sdga_font_face_count(const sdga_font *f);
SDGE_API float sdga_font_face_size(const sdga_font *f, int face);
/* the face that fits `size` the closest (-1 when there is none) */
SDGE_API int sdga_font_face_for(const sdga_font *f, float size);
/* the glyph of a codepoint inside a face (NULL when there is none) */
SDGE_API const sdga_glyph *sdga_font_face_glyph(const sdga_font *f, int face,
                                                unsigned codepoint);




SDGE_API float sdga_font_ascent(const sdga_font *f);
SDGE_API float sdga_font_descent(const sdga_font *f);      /* positive */
SDGE_API float sdga_font_line_height(const sdga_font *f);

/* the atlas image (8 bit coverage, `tex_w` * `tex_h` bytes) */
SDGE_API const unsigned char *sdga_font_pixels(const sdga_font *f);
SDGE_API int sdga_font_texture_size(const sdga_font *f, int *w, int *h);

/* the name of the file (or "builtin") */
SDGE_API const char *sdga_font_name(const sdga_font *f);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_ASSETS_FONT_H */
