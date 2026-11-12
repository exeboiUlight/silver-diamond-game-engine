/* gui.h - Silver Diamond Game Engine :: GUI manager
 *
 * A small retained-mode GUI on top of the shader UI renderer: every widget
 * is a plain struct the application owns, the manager keeps the list, the
 * hover/focus state and dispatches the input.
 *
 * Nothing is rasterised on the CPU - widgets only push rectangles and
 * text into the UI batch, the GPU does the rest (see core/graphics/ui.h).
 *
 * Typical frame:
 *     sdge_gui_begin(gui, w, h);
 *     sdge_gui_draw(gui);          // draws every registered widget
 *     sdge_gui_end(gui);            // flushes the batch
 *
 * Input (usually from the window callbacks):
 *     sdge_gui_mouse_move(gui, x, y);
 *     sdge_gui_mouse_button(gui, button, action);
 *     sdge_gui_key(gui, key, action);
 *     sdge_gui_char(gui, codepoint);
 */
#ifndef SDGE_GUI_H
#define SDGE_GUI_H

#include "../../../sdge_def.h"
#include "../../../graphics/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDGE_GUI_MAX_WIDGETS 64

/* widget event types (delivered to sdge_widget::event) */
#define SDGE_WIDGET_CLICK   1
#define SDGE_WIDGET_HOVER   2
#define SDGE_WIDGET_UNHOVER 3
#define SDGE_WIDGET_KEY     4      /* param = the key code     */
#define SDGE_WIDGET_CHAR    5      /* param = the codepoint   */
#define SDGE_WIDGET_FOCUS   6
#define SDGE_WIDGET_BLUR    7
#define SDGE_WIDGET_SCROLL  8      /* param = the scroll dy*100 */

typedef struct sdge_gui sdge_gui;

/* Base of every widget. `draw` and `event` are filled by the concrete
 * widgets (div/button/input) - an application can add its own by filling
 * these two pointers. */
typedef struct sdge_widget {
    float x, y, w, h;
    int   visible;
    int   enabled;
    void (*draw)(sdge_gui *gui, struct sdge_widget *w);
    void (*event)(sdge_gui *gui, struct sdge_widget *w, int type, int param);
} sdge_widget;

/* --------------------------------------------------------------- manager */

SDGE_API sdge_gui *sdge_gui_create(void);
SDGE_API void      sdge_gui_destroy(sdge_gui *g);

/* registers a widget; returns its index or -1 when the list is full */
SDGE_API int  sdge_gui_add(sdge_gui *g, sdge_widget *w);
SDGE_API void sdge_gui_remove(sdge_gui *g, sdge_widget *w);
SDGE_API void sdge_gui_clear(sdge_gui *g);
SDGE_API int  sdge_gui_count(const sdge_gui *g);

/* frame */
SDGE_API void sdge_gui_begin(sdge_gui *g, int width, int height);
SDGE_API void sdge_gui_draw(sdge_gui *g);
SDGE_API void sdge_gui_end(sdge_gui *g);

/* input */
SDGE_API void sdge_gui_mouse_move(sdge_gui *g, float x, float y);
SDGE_API void sdge_gui_mouse_button(sdge_gui *g, int button, int action);
SDGE_API void sdge_gui_key(sdge_gui *g, int key, int action);
SDGE_API void sdge_gui_char(sdge_gui *g, unsigned codepoint);
SDGE_API void sdge_gui_scroll(sdge_gui *g, float dy);

/* state */
SDGE_API int  sdge_gui_hovered(const sdge_gui *g);   /* widget index or -1 */
SDGE_API int  sdge_gui_focused(const sdge_gui *g);   /* widget index or -1 */
SDGE_API void sdge_gui_set_focus(sdge_gui *g, int index);
SDGE_API void sdge_gui_mouse_pos(const sdge_gui *g, float *x, float *y);

/* the size of the last frame */
SDGE_API void sdge_gui_viewport(const sdge_gui *g, int *w, int *h);

/* -------------------------------------------------------------- helpers */

/* true when the point is inside the widget rectangle */
SDGE_API int sdge_widget_contains(const sdge_widget *w, float x, float y);

/* drawing helpers used by the widgets (and by applications) */
SDGE_API void sdge_gui_fill(sdge_gui *g, float x, float y, float w, float h,
                             const float color[4]);
SDGE_API void sdge_gui_round(sdge_gui *g, float x, float y, float w, float h,
                              float radius, const float color[4]);
SDGE_API void sdge_gui_box(sdge_gui *g, float x, float y, float w, float h,
                            float radius, const float color[4],
                            const float border_color[4], float border_width);
SDGE_API void sdge_gui_shadow(sdge_gui *g, float x, float y, float w, float h,
                              float radius, float softness,
                              const float color[4]);
SDGE_API void sdge_gui_text(sdge_gui *g, float x, float y, const char *text,
                            float size, const float color[4]);
SDGE_API void sdge_gui_text_center(sdge_gui *g, float x, float y,
                                   float w, float h, const char *text,
                                   float size, const float color[4]);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_H */
