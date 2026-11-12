/* button.c - Silver Diamond Game Engine :: GUI button implementation */
#include "button.h"

#include <string.h>

static const float BTN_IDLE[4]      = { 0.22f, 0.24f, 0.30f, 1.0f };
static const float BTN_HOVER[4]     = { 0.30f, 0.34f, 0.44f, 1.0f };
static const float BTN_ACTIVE[4]    = { 0.16f, 0.18f, 0.24f, 1.0f };
static const float BTN_DISABLED[4]  = { 0.16f, 0.17f, 0.20f, 1.0f };
static const float BTN_TEXT[4]      = { 0.92f, 0.93f, 0.96f, 1.0f };
static const float BTN_TEXT_OFF[4]  = { 0.50f, 0.51f, 0.55f, 1.0f };
static const float BTN_BORDER[4]    = { 0.36f, 0.40f, 0.52f, 1.0f };

static void button_draw(sdge_gui *gui, sdge_widget *w) {
    sdge_gui_button *b = (sdge_gui_button *)w;
    if (!b) return;

    const float *fill = b->color;
    if (!b->base.enabled)      fill = b->color_disabled;
    else if (b->active)         fill = b->color_active;
    else if (b->hovered)        fill = b->color_hover;

    const float *txt = b->base.enabled ? b->text_color : b->text_color_disabled;

    if (b->shadow_color[3] > 0.0f && b->base.enabled) {
        sdge_gui_shadow(gui, b->base.x, b->base.y + b->shadow_offset_y,
                        b->base.w, b->base.h, b->radius + b->shadow_softness,
                        b->shadow_softness, b->shadow_color);
    }
    sdge_gui_box(gui, b->base.x, b->base.y, b->base.w, b->base.h, b->radius,
                 fill, b->border_color, b->border_width);

    if (b->label[0]) {
        float tw = sdui_text_width(b->label, b->text_size);
        float th = sdui_text_height(b->text_size);
        sdge_gui_text(gui, b->base.x + (b->base.w - tw) * 0.5f,
                      b->base.y + (b->base.h - th) * 0.5f,
                      b->label, b->text_size, txt);
    }
}

static void button_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    sdge_gui_button *b = (sdge_gui_button *)w;
    if (!b) return;
    (void)gui; (void)param;

    switch (type) {
    case SDGE_WIDGET_HOVER:   b->hovered = 1; break;
    case SDGE_WIDGET_UNHOVER: b->hovered = 0; b->active = 0; break;
    case SDGE_WIDGET_CLICK:
        b->active = 0;
        if (b->base.enabled && b->on_click) b->on_click(b->user);
        break;
    default: break;
    }
}

void sdge_gui_button_init(sdge_gui_button *b, float x, float y,
                          float w, float h, const char *label,
                          void (*on_click)(void *user), void *user) {
    if (!b) return;
    memset(b, 0, sizeof(*b));
    b->base.x = x; b->base.y = y; b->base.w = w; b->base.h = h;
    b->base.visible = 1;
    b->base.enabled = 1;
    b->base.draw = button_draw;
    b->base.event = button_event;

    b->text_size = 16.0f;
    b->radius = 6.0f;
    b->border_width = 1.0f;
    b->shadow_offset_y = 3.0f;
    b->shadow_softness = 5.0f;
    b->on_click = on_click;
    b->user = user;

    memcpy(b->color, BTN_IDLE, sizeof(BTN_IDLE));
    memcpy(b->color_hover, BTN_HOVER, sizeof(BTN_HOVER));
    memcpy(b->color_active, BTN_ACTIVE, sizeof(BTN_ACTIVE));
    memcpy(b->color_disabled, BTN_DISABLED, sizeof(BTN_DISABLED));
    memcpy(b->text_color, BTN_TEXT, sizeof(BTN_TEXT));
    memcpy(b->text_color_disabled, BTN_TEXT_OFF, sizeof(BTN_TEXT_OFF));
    memcpy(b->border_color, BTN_BORDER, sizeof(BTN_BORDER));

    if (label) {
        strncpy(b->label, label, sizeof(b->label) - 1);
        b->label[sizeof(b->label) - 1] = '\0';
    }
}

void sdge_gui_button_set_label(sdge_gui_button *b, const char *label) {
    if (!b) return;
    if (!label) { b->label[0] = '\0'; return; }
    strncpy(b->label, label, sizeof(b->label) - 1);
    b->label[sizeof(b->label) - 1] = '\0';
}
