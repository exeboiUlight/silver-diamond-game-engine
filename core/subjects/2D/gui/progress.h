/* progress.h - Silver Diamond Game Engine :: GUI progress bar
 *
 * A bar that shows a value between 0 and 1.
 */
#ifndef SDGE_GUI_PROGRESS_H
#define SDGE_GUI_PROGRESS_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_gui_progress {
    sdge_widget base;        /* must stay first */

    float value;             /* 0..1 */
    float radius;
    float color[4];          /* the track */
    float fill_color[4];     /* the filled part */
    float border_color[4];
    float border_width;
} sdge_gui_progress;

SDGE_API void sdge_gui_progress_init(sdge_gui_progress *p, float x, float y,
                                      float w, float h);
SDGE_API void sdge_gui_progress_set(sdge_gui_progress *p, float value);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_PROGRESS_H */
