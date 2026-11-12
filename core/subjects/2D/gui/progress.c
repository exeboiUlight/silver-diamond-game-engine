/* progress.c - Silver Diamond Game Engine :: GUI progress bar implementation */
#include "progress.h"

#include <string.h>

static void progress_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_progress *p = (sdge_gui_progress *)w;
    if (!p) return;
    sdge_gui_box(gui, p->base.x, p->base.y, p->base.w, p->base.h, p->radius,
                 p->color, p->border_color, p->border_width);
    float v = p->value;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (v > 0.0f) {
        float w = p->base.w * v;
        float r = p->radius;
        /* the rounded corners are only on the filled part */
        sdge_gui_round(gui, p->base.x, p->base.y, w, p->base.h,
                       (w < p->base.h) ? r : r, p->fill_color);
    }
}

void sdge_gui_progress_init(sdge_gui_progress *p, float x, float y, float w,
                            float h) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->base.x = x; p->base.y = y; p->base.w = w; p->base.h = h;
    p->base.visible = 1;
    p->base.enabled = 1;
    p->base.draw = progress_draw;
    p->radius = 4.0f;
    p->border_width = 1.0f;
    {
        const float track[4] = { 0.13f, 0.14f, 0.18f, 1.0f };
        const float fill[4] = { 0.35f, 0.70f, 0.95f, 1.0f };
        const float border[4] = { 0.28f, 0.31f, 0.40f, 1.0f };
        memcpy(p->color, track, sizeof(track));
        memcpy(p->fill_color, fill, sizeof(fill));
        memcpy(p->border_color, border, sizeof(border));
    }
}

void sdge_gui_progress_set(sdge_gui_progress *p, float value) {
    if (p) p->value = value;
}
