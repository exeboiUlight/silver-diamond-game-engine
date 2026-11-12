/* input.c - Silver Diamond Game Engine :: GUI text input implementation */
#include "input.h"

#include <string.h>

#include "../../../graphics/window.h"   /* key codes */

static const float IN_BG[4]        = { 0.13f, 0.14f, 0.18f, 1.0f };
static const float IN_TEXT[4]      = { 0.94f, 0.95f, 0.97f, 1.0f };
static const float IN_PLACEHOLDER[4] = { 0.45f, 0.47f, 0.52f, 1.0f };
static const float IN_CARET[4]     = { 0.40f, 0.80f, 1.00f, 1.0f };
static const float IN_BORDER[4]    = { 0.28f, 0.30f, 0.38f, 1.0f };
static const float IN_BORDER_F[4]  = { 0.35f, 0.70f, 1.00f, 1.0f };

static void input_changed(sdge_gui_input *in) {
    if (in->on_change) in->on_change(in->user, in->text);
}

static void input_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_input *in = (sdge_gui_input *)w;
    if (!in) return;

    const float *border = in->focused ? in->border_focus_color
                                       : in->border_color;
    sdge_gui_box(gui, in->base.x, in->base.y, in->base.w, in->base.h,
                 in->radius, in->color, border, in->border_width);

    float th = sdui_text_height(in->text_size);
    float ty = in->base.y + (in->base.h - th) * 0.5f;
    float tx = in->base.x + 8.0f;

    if (in->text[0]) {
        sdge_gui_text(gui, tx, ty, in->text, in->text_size, in->text_color);
    } else if (in->placeholder[0]) {
        sdge_gui_text(gui, tx, ty, in->placeholder, in->text_size,
                      in->placeholder_color);
    }

    if (in->focused) {
        /* the caret sits behind the current character */
        float adv = sdui_text_width(in->text, in->text_size);
        float scale = in->text_size / (float)SDUI_FONT_H;
        float cw = (float)SDUI_FONT_W * scale;
        float cx = tx + adv - (float)(strlen(in->text) - (size_t)in->caret)
                                  * ((float)SDUI_FONT_W + 1.0f) * scale;
        (void)cw;
        sdge_gui_fill(gui, cx + 1.0f, ty, 1.5f, th, in->caret_color);
    }
}

static void input_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    sdge_gui_input *in = (sdge_gui_input *)w;
    if (!in) return;
    (void)gui;

    switch (type) {
    case SDGE_WIDGET_FOCUS: in->focused = 1; break;
    case SDGE_WIDGET_BLUR:  in->focused = 0; break;

    case SDGE_WIDGET_CHAR: {
        if (!in->base.enabled) break;
        int len = (int)strlen(in->text);
        if (in->max_len > 0 && len >= in->max_len) break;
        if (param < 32 || param > 126) break;
        /* shift the tail to make room */
        for (int i = len; i >= in->caret; --i)
            in->text[i + 1] = in->text[i];
        in->text[in->caret] = (char)param;
        in->text[len + 1] = '\0';
        in->caret++;
        input_changed(in);
        break;
    }

    case SDGE_WIDGET_KEY: {
        if (!in->base.enabled) break;
        int len = (int)strlen(in->text);
        switch (param) {
        case SDGE_KEY_BACKSPACE:
            if (in->caret > 0) {
                for (int i = in->caret - 1; i < len; ++i)
                    in->text[i] = in->text[i + 1];
                in->caret--;
                input_changed(in);
            }
            break;
        case SDGE_KEY_DELETE:
            if (in->caret < len) {
                for (int i = in->caret; i < len; ++i)
                    in->text[i] = in->text[i + 1];
                input_changed(in);
            }
            break;
        case SDGE_KEY_LEFT:
            if (in->caret > 0) in->caret--;
            break;
        case SDGE_KEY_RIGHT:
            if (in->caret < len) in->caret++;
            break;
        case SDGE_KEY_HOME: in->caret = 0; break;
        case SDGE_KEY_END:  in->caret = len; break;
        default: break;
        }
        break;
    }

    default: break;
    }
}

void sdge_gui_input_init(sdge_gui_input *in, float x, float y,
                         float w, float h, const char *placeholder) {
    if (!in) return;
    memset(in, 0, sizeof(*in));
    in->base.x = x; in->base.y = y; in->base.w = w; in->base.h = h;
    in->base.visible = 1;
    in->base.enabled = 1;
    in->base.draw = input_draw;
    in->base.event = input_event;

    in->text_size = 16.0f;
    in->max_len = (int)sizeof(in->text) - 1;
    in->caret = 0;
    in->radius = 6.0f;
    in->border_width = 1.0f;

    memcpy(in->color, IN_BG, sizeof(IN_BG));
    memcpy(in->text_color, IN_TEXT, sizeof(IN_TEXT));
    memcpy(in->placeholder_color, IN_PLACEHOLDER, sizeof(IN_PLACEHOLDER));
    memcpy(in->caret_color, IN_CARET, sizeof(IN_CARET));
    memcpy(in->border_color, IN_BORDER, sizeof(IN_BORDER));
    memcpy(in->border_focus_color, IN_BORDER_F, sizeof(IN_BORDER_F));

    if (placeholder) {
        strncpy(in->placeholder, placeholder, sizeof(in->placeholder) - 1);
        in->placeholder[sizeof(in->placeholder) - 1] = '\0';
    }
}

const char *sdge_gui_input_text(const sdge_gui_input *in) {
    return in ? in->text : "";
}

void sdge_gui_input_set_text(sdge_gui_input *in, const char *text) {
    if (!in) return;
    if (!text) { in->text[0] = '\0'; in->caret = 0; return; }
    strncpy(in->text, text, sizeof(in->text) - 1);
    in->text[sizeof(in->text) - 1] = '\0';
    in->caret = (int)strlen(in->text);
}

void sdge_gui_input_clear(sdge_gui_input *in) {
    if (!in) return;
    in->text[0] = '\0';
    in->caret = 0;
}
