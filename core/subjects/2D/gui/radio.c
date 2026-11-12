/* radio.c - Silver Diamond Game Engine :: GUI radio button implementation */
#include "radio.h"

#include <string.h>

static void radio_group_sync(sdge_gui_radio_group *g, int selected) {
    if (!g) return;
    for (int i = 0; i < g->count; ++i) {
        if (g->items[i]) g->items[i]->selected = (g->items[i]->index == selected);
    }
}

static void radio_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_radio *r = (sdge_gui_radio *)w;
    if (!r) return;
    float s = r->size;
    /* the outer ring */
    sdge_gui_round(gui, r->base.x, r->base.y, s, s, s * 0.5f, r->color);
    /* the inner dot */
    if (r->selected) {
        float d = s * 0.45f;
        float o = (s - d) * 0.5f;
        sdge_gui_round(gui, r->base.x + o, r->base.y + o, d, d, d * 0.5f,
                       r->dot_color);
    }
    if (r->label[0]) {
        float th = sdui_text_height(r->label_size);
        sdge_gui_text(gui, r->base.x + s + 6.0f,
                      r->base.y + (s - th) * 0.5f, r->label, r->label_size,
                      r->border_color);
    }
}

static void radio_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    sdge_gui_radio *r = (sdge_gui_radio *)w;
    if (!r) return;
    (void)gui; (void)param;
    if (type != SDGE_WIDGET_CLICK) return;
    if (r->group) {
        if (r->group->selected != r->index) {
            r->group->selected = r->index;
            radio_group_sync(r->group, r->index);
            if (r->group->on_change) r->group->on_change(r->group->user,
                                                         r->index);
        }
    }
    r->selected = 1;
}

void sdge_gui_radio_group_init(sdge_gui_radio_group *g) {
    if (!g) return;
    memset(g, 0, sizeof(*g));
    g->selected = -1;
}

SDGE_API void sdge_gui_radio_init(sdge_gui_radio *r, float x, float y, float size,
                                  int index, sdge_gui_radio_group *group) {
    if (!r) return;
    memset(r, 0, sizeof(*r));
    r->base.x = x; r->base.y = y;
    r->base.w = size; r->base.h = size;
    r->base.visible = 1;
    r->base.enabled = 1;
    r->base.draw = radio_draw;
    r->base.event = radio_event;
    r->size = size > 0.0f ? size : 16.0f;
    r->index = index;
    r->group = group;
    r->label_size = 13.0f;
    {
        const float bg[4] = { 0.13f, 0.14f, 0.18f, 1.0f };
        const float dot[4] = { 0.40f, 0.80f, 0.95f, 1.0f };
        const float border[4] = { 0.34f, 0.37f, 0.46f, 1.0f };
        memcpy(r->color, bg, sizeof(bg));
        memcpy(r->dot_color, dot, sizeof(dot));
        memcpy(r->border_color, border, sizeof(border));
    }
    if (group) {
        if (group->count >= SDGE_GUI_MAX_WIDGETS) return;
        group->items[group->count++] = r;
        r->selected = (group->selected == index);
        if (group->selected < 0) {
            group->selected = index;
            r->selected = 1;
        }
    }
}

void sdge_gui_radio_select(sdge_gui_radio_group *g, int index) {
    if (!g) return;
    g->selected = index;
    radio_group_sync(g, index);
}
