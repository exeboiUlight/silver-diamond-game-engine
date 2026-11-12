/* checkbox.h - Silver Diamond Game Engine :: GUI checkbox
 *
 * A box with a tick. Clicking it toggles the state.
 */
#ifndef SDGE_GUI_CHECKBOX_H
#define SDGE_GUI_CHECKBOX_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_gui_checkbox {
    sdge_widget base;        /* must stay first */

    int   checked;
    float size;              /* the box size in pixels */
    char  label[32];         /* the text next to the box */
    float label_size;
    float color[4];
    float tick_color[4];
    float border_color[4];
    float border_width;
    float radius;

    void (*on_change)(void *user, int checked);
    void  *user;
} sdge_gui_checkbox;

SDGE_API void sdge_gui_checkbox_init(sdge_gui_checkbox *c, float x, float y,
                                      float size, const char *label,
                                      int checked);
SDGE_API void sdge_gui_checkbox_set(sdge_gui_checkbox *c, int checked);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_CHECKBOX_H */
