/* listbox.c - Silver Diamond Game Engine :: GUI list box implementation */
#include "listbox.h"

#include <string.h>

static void listbox_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_listbox *l = (sdge_gui_listbox *)w;
    if (!l) return;
    sdge_gui_box(gui, l->base.x, l->base.y, l->base.w, l->base.h, l->radius,
                 l->color, l->border_color, l->border_width);

    float th = sdui_text_height(l->size);
    int visible = (int)((l->base.h - 4.0f) / l->row_height);
    if (visible < 1) visible = 1;
    if (visible > l->count) visible = l->count;

    for (int i = 0; i < visible; ++i) {
        int idx = l->scroll + i;
        if (idx >= l->count) break;
        float ry = l->base.y + 2.0f + (float)i * l->row_height;
        if (idx == l->selected)
            sdge_gui_fill(gui, l->base.x + 2.0f, ry, l->base.w - 4.0f,
                          l->row_height, l->color_selected);
        sdge_gui_text(gui, l->base.x + 8.0f, ry + (l->row_height - th) * 0.5f,
                      l->items[idx], l->size,
                      (idx == l->selected) ? l->text_selected_color
                                           : l->text_color);
    }
}

static void listbox_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    sdge_gui_listbox *l = (sdge_gui_listbox *)w;
    if (!l) return;
    (void)gui;
    (void)param;
    if (type != SDGE_WIDGET_CLICK) return;

    float mx = 0.0f, my = 0.0f;
    sdge_gui_mouse_pos(gui, &mx, &my);
    if (my < l->base.y || my > l->base.y + l->base.h) return;
    int row = (int)((my - l->base.y - 2.0f) / l->row_height);
    int idx = l->scroll + row;
    if (idx < 0 || idx >= l->count) return;
    l->selected = idx;
    if (l->on_select) l->on_select(l->user, idx);
}

void sdge_gui_listbox_init(sdge_gui_listbox *l, float x, float y, float w,
                           float h) {
    if (!l) return;
    memset(l, 0, sizeof(*l));
    l->base.x = x; l->base.y = y; l->base.w = w; l->base.h = h;
    l->base.visible = 1;
    l->base.enabled = 1;
    l->base.draw = listbox_draw;
    l->base.event = listbox_event;
    l->selected = -1;
    l->row_height = 22.0f;
    l->size = 14.0f;
    l->radius = 5.0f;
    l->border_width = 1.0f;
    {
        const float bg[4] = { 0.11f, 0.12f, 0.16f, 1.0f };
        const float sel[4] = { 0.20f, 0.36f, 0.60f, 1.0f };
        const float txt[4] = { 0.86f, 0.88f, 0.92f, 1.0f };
        const float stxt[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        const float border[4] = { 0.28f, 0.31f, 0.40f, 1.0f };
        memcpy(l->color, bg, sizeof(bg));
        memcpy(l->color_selected, sel, sizeof(sel));
        memcpy(l->text_color, txt, sizeof(txt));
        memcpy(l->text_selected_color, stxt, sizeof(stxt));
        memcpy(l->border_color, border, sizeof(border));
    }
}

int sdge_gui_listbox_add(sdge_gui_listbox *l, const char *item) {
    if (!l || !item) return -1;
    if (l->count >= SDGE_GUI_LIST_MAX) return -1;
    strncpy(l->items[l->count], item, sizeof(l->items[0]) - 1);
    l->items[l->count][sizeof(l->items[0]) - 1] = '\0';
    return l->count++;
}

void sdge_gui_listbox_select(sdge_gui_listbox *l, int index) {
    if (!l) return;
    if (index >= 0 && index < l->count) l->selected = index;
}

const char *sdge_gui_listbox_selected(const sdge_gui_listbox *l) {
    if (!l || l->selected < 0 || l->selected >= l->count) return NULL;
    return l->items[l->selected];
}
