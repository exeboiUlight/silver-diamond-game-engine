/* slider.c - Silver Diamond Game Engine :: GUI slider implementation */
#include "slider.h"

#include <string.h>
#include <math.h>

static float slider_knob_x(const sdge_gui_slider *s) {
    float usable = s->base.w - s->knob_size;
    if (usable <= 0.0f) usable = 1.0f;
    return s->base.x + usable * s->value;
}

static void slider_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_slider *s = (sdge_gui_slider *)w;
    if (!s) return;
    float th = s->track_height;
    float ty = s->base.y + (s->base.h - th) * 0.5f;
    sdge_gui_round(gui, s->base.x, ty, s->base.w, th, th * 0.5f, s->color);
    float kx = slider_knob_x(s);
    float usable = s->base.w - s->knob_size;
    if (usable > 0.0f && s->value > 0.0f)
        sdge_gui_round(gui, s->base.x, ty, usable * s->value + s->knob_size * 0.5f,
                       th, th * 0.5f, s->fill_color);
    float ks = s->knob_size;
    sdge_gui_round(gui, kx, s->base.y + (s->base.h - ks) * 0.5f, ks, ks,
                   ks * 0.25f, s->knob_color);
}

static void slider_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    sdge_gui_slider *s = (sdge_gui_slider *)w;
    if (!s) return;
    (void)param;

    switch (type) {
    case SDGE_WIDGET_HOVER:
        if (s->dragging) {
            float mx = 0.0f, my = 0.0f;
            sdge_gui_mouse_pos(gui, &mx, &my);
            float usable = s->base.w - s->knob_size;
            float v = (usable > 0.0f) ? (mx - s->base.x - s->knob_size * 0.5f) / usable : 0.0f;
            if (v < 0.0f) v = 0.0f;
            if (v > 1.0f) v = 1.0f;
            if (v != s->value) {
                s->value = v;
                if (s->on_change) s->on_change(s->user, v);
            }
        }
        break;
    case SDGE_WIDGET_CLICK:
        s->dragging = 0;
        break;
    default:
        break;
    }
}

void sdge_gui_slider_init(sdge_gui_slider *s, float x, float y, float w,
                          float h, float value) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->base.x = x; s->base.y = y; s->base.w = w; s->base.h = h;
    s->base.visible = 1;
    s->base.enabled = 1;
    s->base.draw = slider_draw;
    s->base.event = slider_event;
    s->value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
    s->knob_size = 16.0f;
    s->track_height = 6.0f;
    s->radius = 3.0f;
    s->border_width = 1.0f;
    {
        const float track[4] = { 0.16f, 0.17f, 0.22f, 1.0f };
        const float fill[4] = { 0.35f, 0.70f, 0.95f, 1.0f };
        const float knob[4] = { 0.86f, 0.88f, 0.93f, 1.0f };
        const float kborder[4] = { 0.30f, 0.33f, 0.42f, 1.0f };
        memcpy(s->color, track, sizeof(track));
        memcpy(s->fill_color, fill, sizeof(fill));
        memcpy(s->knob_color, knob, sizeof(knob));
        memcpy(s->knob_border, kborder, sizeof(kborder));
    }
}

void sdge_gui_slider_set(sdge_gui_slider *s, float value) {
    if (!s) return;
    s->value = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}
