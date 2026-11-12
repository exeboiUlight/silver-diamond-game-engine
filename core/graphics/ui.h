/* ui.h - Silver Diamond Game Engine :: 2D UI renderer (shader only)
 *
 * The whole GUI is drawn by ONE program: rounded boxes are signed distance
 * fields evaluated per fragment, text is a quad per glyph sampled from a
 * font atlas that is generated at runtime. Nothing is rasterised on the
 * CPU - the CPU only fills a vertex buffer.
 *
 * Batching: every rect/glyph of a frame goes into one dynamic buffer and
 * leaves in a single draw call.
 *
 * Typical frame:
 *     sdui_init();                          // once, after the GL context
 *     sdui_begin(w, h);
 *     sdui_rect(&r);
 *     sdui_text(x, y, "hello", 16.0f, color);
 *     sdui_end();                           // flushes the batch
 */
#ifndef SDGE_UI_H
#define SDGE_UI_H

#include "../sdge_def.h"
#include "../assets/font.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------- shapes */

/* A rounded rectangle. Everything is optional:
 *   radius        corner radius in pixels (0 = sharp corners)
 *   border_width  border thickness in pixels (0 = no border)
 *   softness      > 0 turns the shape into a soft blob (shadow)
 *   mode          SDUI_MODE_BOX / SDUI_MODE_TEXT / SDUI_MODE_SOFT
 */
#define SDUI_MODE_BOX  0
#define SDUI_MODE_TEXT 1
#define SDUI_MODE_SOFT 2

typedef struct {
    float x, y, w, h;
    float radius;
    float border_width;
    float softness;
    int   mode;
    float color[4];        /* fill (rgba) */
    float border_color[4]; /* border (rgba) */
} sdui_rect;

/* --------------------------------------------------------------- device */

/* compiles the program and builds the font atlas; 0 on success.
 * Call AFTER the GL context is current. */
SDGE_API int  sdui_init(void);
SDGE_API void sdui_shutdown(void);

/* switches the UI to another font (a TTF/OTF loaded with
 * sdga_load_font). The font must stay alive while the UI uses it.
 * NULL restores the built-in bitmap font. */
SDGE_API int  sdui_set_font(sdga_font *font);
SDGE_API const sdga_font *sdui_get_font(void);

/* starts/ends a batch; `w`/`h` are the framebuffer size in pixels */
SDGE_API void sdui_begin(int width, int height);
SDGE_API void sdui_end(void);

/* how many quads are in the current batch (diagnostics/tests) */
SDGE_API int  sdui_quad_count(void);

/* ---------------------------------------------------------------- draw */

/* appends a rectangle to the batch */
SDGE_API void sdui_draw_rect(const sdui_rect *r);

/* appends a filled rectangle (the common case) */
SDGE_API void sdui_box(float x, float y, float w, float h,
                        const float color[4]);

/* appends a text run; `size` is the cap height in pixels.
 * the colour is rgba. Returns the advance width of the run. */
SDGE_API float sdui_text(float x, float y, const char *utf8, float size,
                          const float color[4]);

/* text centred inside the rectangle (x,y,w,h) */
SDGE_API float sdui_text_centered(float x, float y, float w, float h,
                                   const char *utf8, float size,
                                   const float color[4]);

/* --------------------------------------------------------------- text */

/* width/height of a text run in pixels */
SDGE_API float sdui_text_width(const char *utf8, float size);
SDGE_API float sdui_text_height(float size);

/* the built-in font is 5x7 cells; `size` scales it */
#define SDUI_FONT_W 5
#define SDUI_FONT_H 7

#ifdef __cplusplus
}
#endif
#endif /* SDGE_UI_H */
