/* div.h - Silver Diamond Game Engine :: GUI panel (div)
 *
 * The simplest widget: a rounded rectangle with an optional border and a
 * soft shadow. Drawn entirely by the UI shader.
 */
#ifndef SDGE_GUI_DIV_H
#define SDGE_GUI_DIV_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_gui_div {
    sdge_widget base;        /* must stay first */

    float color[4];
    float border_color[4];
    float border_width;
    float radius;

    /* soft shadow behind the panel (alpha 0 disables it) */
    float shadow_color[4];
    float shadow_offset_y;
    float shadow_softness;
} sdge_gui_div;

/* creates a panel; `color` may be NULL for a transparent one */
SDGE_API void sdge_gui_div_init(sdge_gui_div *d, float x, float y,
                                 float w, float h, const float color[4]);

/* sets the panel rectangle */
SDGE_API void sdge_gui_div_set_rect(sdge_gui_div *d, float x, float y,
                                    float w, float h);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_DIV_H */
