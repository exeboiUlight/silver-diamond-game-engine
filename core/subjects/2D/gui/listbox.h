/* listbox.h - Silver Diamond Game Engine :: GUI list box
 *
 * A vertical list of items; clicking an item selects it.
 */
#ifndef SDGE_GUI_LISTBOX_H
#define SDGE_GUI_LISTBOX_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDGE_GUI_LIST_MAX 32

typedef struct sdge_gui_listbox {
    sdge_widget base;        /* must stay first */

    char  items[SDGE_GUI_LIST_MAX][48];
    int   count;
    int   selected;
    int   scroll;
    float row_height;
    float size;              /* the font size */
    float radius;
    float color[4];
    float color_selected[4];
    float text_color[4];
    float text_selected_color[4];
    float border_color[4];
    float border_width;

    void (*on_select)(void *user, int index);
    void  *user;
} sdge_gui_listbox;

SDGE_API void sdge_gui_listbox_init(sdge_gui_listbox *l, float x, float y,
                                    float w, float h);
SDGE_API int  sdge_gui_listbox_add(sdge_gui_listbox *l, const char *item);
SDGE_API void sdge_gui_listbox_select(sdge_gui_listbox *l, int index);
SDGE_API const char *sdge_gui_listbox_selected(const sdge_gui_listbox *l);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_LISTBOX_H */
