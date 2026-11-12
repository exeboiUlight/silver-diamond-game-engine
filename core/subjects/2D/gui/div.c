/* div.c - Silver Diamond Game Engine :: GUI panel (div) implementation */
#include "div.h"

#include <string.h>

static void div_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_div *d = (sdge_gui_div *)w;
    if (!d) return;

    if (d->shadow_color[3] > 0.0f) {
        sdge_gui_shadow(gui, d->base.x, d->base.y + d->shadow_offset_y,
                        d->base.w, d->base.h, d->radius + d->shadow_softness,
                        d->shadow_softness, d->shadow_color);
    }
    sdge_gui_box(gui, d->base.x, d->base.y, d->base.w, d->base.h, d->radius,
                 d->color, d->border_color, d->border_width);
}

void sdge_gui_div_init(sdge_gui_div *d, float x, float y, float w, float h,
                       const float color[4]) {
    if (!d) return;
    memset(d, 0, sizeof(*d));
    d->base.x = x; d->base.y = y; d->base.w = w; d->base.h = h;
    d->base.visible = 1;
    d->base.enabled = 1;
    d->base.draw = div_draw;

    d->radius = 8.0f;
    d->border_width = 0.0f;
    d->shadow_offset_y = 3.0f;
    d->shadow_softness = 6.0f;
    if (color) memcpy(d->color, color, sizeof(d->color));
}

void sdge_gui_div_set_rect(sdge_gui_div *d, float x, float y,
                           float w, float h) {
    if (!d) return;
    d->base.x = x; d->base.y = y; d->base.w = w; d->base.h = h;
}
