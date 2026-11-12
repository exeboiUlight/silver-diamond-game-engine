/* slider.h - Silver Diamond Game Engine :: GUI slider
 *
 * A horizontal slider with a draggable knob. The value is 0..1.
 */
#ifndef SDGE_GUI_SLIDER_H
#define SDGE_GUI_SLIDER_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_gui_slider {
    sdge_widget base;        /* must stay first */

    float value;             /* 0..1 */
    float knob_size;
    float track_height;
    float radius;
    float color[4];          /* the track */
    float fill_color[4];     /* the filled part */
    float knob_color[4];
    float knob_border[4];
    float border_width;

    int   dragging;

    void (*on_change)(void *user, float value);
    void  *user;
} sdge_gui_slider;

SDGE_API void sdge_gui_slider_init(sdge_gui_slider *s, float x, float y,
                                   float w, float h, float value);
SDGE_API void sdge_gui_slider_set(sdge_gui_slider *s, float value);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_SLIDER_H */
