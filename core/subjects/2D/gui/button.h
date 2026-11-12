/* button.h - Silver Diamond Game Engine :: GUI button
 *
 * A clickable rounded rectangle with a centred label. Hover and pressed
 * states are separate colours; the widget reports clicks through the
 * on_click callback.
 */
#ifndef SDGE_GUI_BUTTON_H
#define SDGE_GUI_BUTTON_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_gui_button {
    sdge_widget base;        /* must stay first */

    char   label[64];
    float  text_size;
    float  radius;

    float  color[4];         /* idle      */
    float  color_hover[4];
    float  color_active[4];
    float  color_disabled[4];
    float  text_color[4];
    float  text_color_disabled[4];

    float  border_color[4];
    float  border_width;

    float  shadow_color[4];
    float  shadow_offset_y;
    float  shadow_softness;

    void (*on_click)(void *user);
    void  *user;

    int hovered;             /* internal state */
    int active;              /* internal state */
} sdge_gui_button;

/* creates a button; `label` may be NULL */
SDGE_API void sdge_gui_button_init(sdge_gui_button *b, float x, float y,
                                   float w, float h, const char *label,
                                   void (*on_click)(void *user), void *user);

/* changes the label at runtime */
SDGE_API void sdge_gui_button_set_label(sdge_gui_button *b, const char *label);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_BUTTON_H */
