/* label.h - Silver Diamond Game Engine :: GUI text label
 *
 * A simple piece of text with an optional alignment.
 */
#ifndef SDGE_GUI_LABEL_H
#define SDGE_GUI_LABEL_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDGE_GUI_ALIGN_LEFT   0
#define SDGE_GUI_ALIGN_CENTER 1
#define SDGE_GUI_ALIGN_RIGHT  2

typedef struct sdge_gui_label {
    sdge_widget base;        /* must stay first */

    char  text[128];
    float size;
    float color[4];
    int   align;
} sdge_gui_label;

SDGE_API void sdge_gui_label_init(sdge_gui_label *l, float x, float y,
                                   float w, float h, const char *text,
                                   float size, const float color[4]);
SDGE_API void sdge_gui_label_set_text(sdge_gui_label *l, const char *text);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_LABEL_H */
