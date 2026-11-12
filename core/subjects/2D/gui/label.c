/* label.c - Silver Diamond Game Engine :: GUI text label implementation */
#include "label.h"

#include <string.h>

static void label_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_label *l = (sdge_gui_label *)w;
    if (!l || !l->text[0]) return;
    float tw = sdui_text_width(l->text, l->size);
    float th = sdui_text_height(l->size);
    float x = l->base.x;
    if (l->align == SDGE_GUI_ALIGN_CENTER)
        x = l->base.x + (l->base.w - tw) * 0.5f;
    else if (l->align == SDGE_GUI_ALIGN_RIGHT)
        x = l->base.x + l->base.w - tw;
    float y = l->base.y + (l->base.h - th) * 0.5f;
    sdge_gui_text(gui, x, y, l->text, l->size, l->color);
}

void sdge_gui_label_init(sdge_gui_label *l, float x, float y, float w,
                         float h, const char *text, float size,
                         const float color[4]) {
    if (!l) return;
    memset(l, 0, sizeof(*l));
    l->base.x = x; l->base.y = y; l->base.w = w; l->base.h = h;
    l->base.visible = 1;
    l->base.enabled = 1;
    l->base.draw = label_draw;
    l->size = size > 0.0f ? size : 14.0f;
    l->align = SDGE_GUI_ALIGN_LEFT;
    if (color) memcpy(l->color, color, sizeof(l->color));
    sdge_gui_label_set_text(l, text);
}

void sdge_gui_label_set_text(sdge_gui_label *l, const char *text) {
    if (!l) return;
    if (!text) { l->text[0] = '\0'; return; }
    strncpy(l->text, text, sizeof(l->text) - 1);
    l->text[sizeof(l->text) - 1] = '\0';
}
