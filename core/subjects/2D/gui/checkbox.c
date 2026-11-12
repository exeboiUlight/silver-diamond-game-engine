/* checkbox.c - Silver Diamond Game Engine :: GUI checkbox implementation */
#include "checkbox.h"

#include <string.h>

static void checkbox_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_checkbox *c = (sdge_gui_checkbox *)w;
    if (!c) return;
    float s = c->size;
    sdge_gui_box(gui, c->base.x, c->base.y, s, s, c->radius, c->color,
                 c->border_color, c->border_width);
    if (c->checked) {
        /* a tick: two strokes drawn as thin boxes */
        float t = s * 0.18f;
        float x0 = c->base.x + s * 0.22f, y0 = c->base.y + s * 0.52f;
        float x1 = c->base.x + s * 0.42f, y1 = c->base.y + s * 0.74f;
        float x2 = c->base.x + s * 0.78f, y2 = c->base.y + s * 0.28f;
        /* the first segment */
        for (float u = 0.0f; u <= 1.0f; u += 0.25f) {
            float px = x0 + (x1 - x0) * u, py = y0 + (y1 - y0) * u;
            sdge_gui_fill(gui, px - t * 0.5f, py - t * 0.5f, t, t, c->tick_color);
        }
        for (float u = 0.0f; u <= 1.0f; u += 0.2f) {
            float px = x1 + (x2 - x1) * u, py = y1 + (y2 - y1) * u;
            sdge_gui_fill(gui, px - t * 0.5f, py - t * 0.5f, t, t, c->tick_color);
        }
    }
    if (c->label[0]) {
        float th = sdui_text_height(c->label_size);
        sdge_gui_text(gui, c->base.x + s + 8.0f,
                      c->base.y + (s - th) * 0.5f, c->label, c->label_size,
                      c->border_color);
    }
}

static void checkbox_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    sdge_gui_checkbox *c = (sdge_gui_checkbox *)w;
    if (!c) return;
    (void)gui; (void)param;
    if (type == SDGE_WIDGET_CLICK) {
        c->checked = !c->checked;
        if (c->on_change) c->on_change(c->user, c->checked);
    }
}

void sdge_gui_checkbox_init(sdge_gui_checkbox *c, float x, float y, float size,
                            const char *label, int checked) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->base.x = x; c->base.y = y;
    c->base.w = size; c->base.h = size;
    c->base.visible = 1;
    c->base.enabled = 1;
    c->base.draw = checkbox_draw;
    c->base.event = checkbox_event;
    c->size = size > 0.0f ? size : 16.0f;
    c->checked = checked;
    c->radius = 3.0f;
    c->border_width = 1.0f;
    c->label_size = 13.0f;
    {
        const float bg[4] = { 0.13f, 0.14f, 0.18f, 1.0f };
        const float tick[4] = { 0.40f, 0.85f, 0.60f, 1.0f };
        const float border[4] = { 0.30f, 0.33f, 0.42f, 1.0f };
        memcpy(c->color, bg, sizeof(bg));
        memcpy(c->tick_color, tick, sizeof(tick));
        memcpy(c->border_color, border, sizeof(border));
    }
    (void)label;
    if (label) {
        strncpy(c->label, label, sizeof(c->label) - 1);
        c->label[sizeof(c->label) - 1] = '\0';
    }
}

void sdge_gui_checkbox_set(sdge_gui_checkbox *c, int checked) {
    if (c) c->checked = checked ? 1 : 0;
}
