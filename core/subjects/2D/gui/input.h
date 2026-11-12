/* input.h - Silver Diamond Game Engine :: GUI text input
 *
 * A single line text field: it takes the keyboard focus when clicked and
 * reports every change through the on_change callback.
 */
#ifndef SDGE_GUI_INPUT_H
#define SDGE_GUI_INPUT_H

#include "gui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDGE_GUI_INPUT_MAX 128

typedef struct sdge_gui_input {
    sdge_widget base;        /* must stay first */

    char  text[SDGE_GUI_INPUT_MAX];
    char  placeholder[64];
    float text_size;
    int   caret;             /* the caret index inside `text` */
    int   max_len;

    float radius;
    float color[4];
    float text_color[4];
    float placeholder_color[4];
    float caret_color[4];
    float border_color[4];
    float border_width;
    float border_focus_color[4];

    int   focused;           /* internal state */

    void (*on_change)(void *user, const char *text);
    void  *user;
} sdge_gui_input;

/* creates a text field; `placeholder` may be NULL */
SDGE_API void sdge_gui_input_init(sdge_gui_input *in, float x, float y,
                                  float w, float h, const char *placeholder);

SDGE_API const char *sdge_gui_input_text(const sdge_gui_input *in);
SDGE_API void sdge_gui_input_set_text(sdge_gui_input *in, const char *text);
SDGE_API void sdge_gui_input_clear(sdge_gui_input *in);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_GUI_INPUT_H */
