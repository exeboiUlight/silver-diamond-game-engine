/* radio.h - Silver Diamond Game Engine :: GUI radio button
 *
 * One button of a radio group: the buttons of a group share a `group`
 * pointer and only one of them can be selected.
 */
#ifndef SDGE_GUI_RADIO_H
#define SDGE_GUI_RADIO_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_gui_radio_group {
    int selected;            /* the index of the selected button */
    int count;
    void (*on_change)(void *user, int index);
    void *user;
} sdge_gui_radio_group;

typedef struct sdge_gui_radio {
    sdge_widget base;        /* must stay first */

    int   index;             /* the index inside the group */
    int   selected;
    float size;
    float radius;
    char  label[32];
    float label_size;
    float color[4];
    float dot_color[4];
    float border_color[4];
    float border_width;
    sdge_gui_radio_group *group;
} sdge_gui_radio;

SDGE_API void sdge_gui_radio_group_init(sdge_gui_radio_group *g);
SDGE_API void sdge_gui_radio_init(sdge_gui_radio *r, float x, float y,
                                  float size, int index,
                                  sdge_gui_radio_group *group);
SDGE_API void sdge_gui_radio_select(sdge_gui_radio_group *g, int index);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_RADIO_H */
