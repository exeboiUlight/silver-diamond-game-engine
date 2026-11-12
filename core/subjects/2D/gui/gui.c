/* gui.c - Silver Diamond Game Engine :: GUI manager implementation */
#include "gui.h"

#include <stdlib.h>
#include <string.h>

struct sdge_gui {
    sdge_widget *widgets[SDGE_GUI_MAX_WIDGETS];
    int count;

    int   hovered;        /* index of the widget under the mouse */
    int   pressed;        /* index of the widget being pressed */
    int   focused;        /* index of the keyboard focused widget */
    float mouse_x, mouse_y;
    int   width, height;
};

/* --------------------------------------------------------------- helpers */

int sdge_widget_contains(const sdge_widget *w, float x, float y) {
    if (!w) return 0;
    return (x >= w->x && x <= w->x + w->w &&
            y >= w->y && y <= w->y + w->h);
}

static void gui_event(sdge_gui *g, int index, int type, int param) {
    if (!g || index < 0 || index >= g->count) return;
    sdge_widget *w = g->widgets[index];
    if (w && w->event) w->event(g, w, type, param);
}

/* the topmost widget that contains the point */
static int gui_hit(const sdge_gui *g, float x, float y) {
    int found = -1;
    for (int i = 0; i < g->count; ++i) {
        const sdge_widget *w = g->widgets[i];
        if (!w || !w->visible || !w->enabled) continue;
        if (sdge_widget_contains(w, x, y)) found = i;
    }
    return found;
}

/* --------------------------------------------------------------- manager */

sdge_gui *sdge_gui_create(void) {
    sdge_gui *g = (sdge_gui *)calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->hovered = -1;
    g->pressed = -1;
    g->focused = -1;
    g->width = 1;
    g->height = 1;
    return g;
}

void sdge_gui_destroy(sdge_gui *g) {
    free(g);
}

int sdge_gui_add(sdge_gui *g, sdge_widget *w) {
    if (!g || !w) return -1;
    if (g->count >= SDGE_GUI_MAX_WIDGETS) return -1;
    g->widgets[g->count++] = w;
    return g->count - 1;
}

void sdge_gui_remove(sdge_gui *g, sdge_widget *w) {
    if (!g || !w) return;
    for (int i = 0; i < g->count; ++i) {
        if (g->widgets[i] != w) continue;
        for (int k = i; k < g->count - 1; ++k)
            g->widgets[k] = g->widgets[k + 1];
        g->count--;
        g->widgets[g->count] = NULL;
        if (g->hovered > i) g->hovered--;
        else if (g->hovered == i) g->hovered = -1;
        if (g->pressed > i) g->pressed--;
        else if (g->pressed == i) g->pressed = -1;
        if (g->focused > i) g->focused--;
        else if (g->focused == i) g->focused = -1;
        return;
    }
}

void sdge_gui_clear(sdge_gui *g) {
    if (!g) return;
    g->count = 0;
    g->hovered = -1;
    g->pressed = -1;
    g->focused = -1;
}

int sdge_gui_count(const sdge_gui *g) { return g ? g->count : 0; }

void sdge_gui_begin(sdge_gui *g, int width, int height) {
    if (!g) return;
    g->width = width > 0 ? width : 1;
    g->height = height > 0 ? height : 1;
    sdui_begin(g->width, g->height);
}

void sdge_gui_draw(sdge_gui *g) {
    if (!g) return;
    for (int i = 0; i < g->count; ++i) {
        sdge_widget *w = g->widgets[i];
        if (!w || !w->visible || !w->draw) continue;
        w->draw(g, w);
    }
}

void sdge_gui_end(sdge_gui *g) {
    if (!g) return;
    sdui_end();
}

/* ----------------------------------------------------------------- input */

void sdge_gui_mouse_move(sdge_gui *g, float x, float y) {
    if (!g) return;
    g->mouse_x = x;
    g->mouse_y = y;

    int hit = gui_hit(g, x, y);
    if (hit == g->hovered) return;
    if (g->hovered >= 0) gui_event(g, g->hovered, SDGE_WIDGET_UNHOVER, 0);
    g->hovered = hit;
    if (hit >= 0) gui_event(g, hit, SDGE_WIDGET_HOVER, 0);
}

void sdge_gui_mouse_button(sdge_gui *g, int button, int action) {
    if (!g || button != 0) return;            /* left button only */
    if (action == 1) {                          /* press */
        g->pressed = gui_hit(g, g->mouse_x, g->mouse_y);
        if (g->pressed >= 0)
            gui_event(g, g->pressed, SDGE_WIDGET_PRESS, 0);
    } else if (action == 0) {                   /* release */
        int hit = gui_hit(g, g->mouse_x, g->mouse_y);
        if (g->pressed >= 0 && g->pressed == hit) {
            gui_event(g, hit, SDGE_WIDGET_CLICK, 0);
            /* a click focuses the widget */
            if (g->focused != hit) {
                if (g->focused >= 0) gui_event(g, g->focused, SDGE_WIDGET_BLUR, 0);
                g->focused = hit;
                gui_event(g, hit, SDGE_WIDGET_FOCUS, 0);
            }
        }
        g->pressed = -1;
    }
}

void sdge_gui_key(sdge_gui *g, int key, int action) {
    if (!g || g->focused < 0) return;
    if (action == 0) return;                   /* only press/repeat */
    gui_event(g, g->focused, SDGE_WIDGET_KEY, key);
}

void sdge_gui_char(sdge_gui *g, unsigned codepoint) {
    if (!g || g->focused < 0) return;
    if (codepoint < 32 || codepoint > 126) return;
    gui_event(g, g->focused, SDGE_WIDGET_CHAR, (int)codepoint);
}

void sdge_gui_scroll(sdge_gui *g, float dy) {
    if (!g) return;
    for (int i = 0; i < g->count; ++i) {
        sdge_widget *w = g->widgets[i];
        if (!w || !w->visible || !w->enabled) continue;
        if (sdge_widget_contains(w, g->mouse_x, g->mouse_y)) {
            gui_event(g, i, SDGE_WIDGET_SCROLL, (int)(dy * 100.0f));
            return;
        }
    }
}

/* ----------------------------------------------------------------- state */

int sdge_gui_hovered(const sdge_gui *g) { return g ? g->hovered : -1; }
int sdge_gui_focused(const sdge_gui *g) { return g ? g->focused : -1; }

void sdge_gui_set_focus(sdge_gui *g, int index) {
    if (!g) return;
    if (index == g->focused) return;
    if (g->focused >= 0) gui_event(g, g->focused, SDGE_WIDGET_BLUR, 0);
    g->focused = index;
    if (index >= 0) gui_event(g, index, SDGE_WIDGET_FOCUS, 0);
}

void sdge_gui_mouse_pos(const sdge_gui *g, float *x, float *y) {
    if (!g) return;
    if (x) *x = g->mouse_x;
    if (y) *y = g->mouse_y;
}

void sdge_gui_viewport(const sdge_gui *g, int *w, int *h) {
    if (!g) return;
    if (w) *w = g->width;
    if (h) *h = g->height;
}

/* --------------------------------------------------------------- drawing */

void sdge_gui_fill(sdge_gui *g, float x, float y, float w, float h,
                   const float color[4]) {
    (void)g;
    sdui_box(x, y, w, h, color);
}

void sdge_gui_round(sdge_gui *g, float x, float y, float w, float h,
                    float radius, const float color[4]) {
    (void)g;
    sdui_rect r;
    memset(&r, 0, sizeof(r));
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.radius = radius;
    r.mode = SDUI_MODE_BOX;
    if (color) memcpy(r.color, color, sizeof(r.color));
    sdui_draw_rect(&r);
}

void sdge_gui_box(sdge_gui *g, float x, float y, float w, float h,
                  float radius, const float color[4],
                  const float border_color[4], float border_width) {
    (void)g;
    sdui_rect r;
    memset(&r, 0, sizeof(r));
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.radius = radius;
    r.border_width = border_width;
    r.mode = SDUI_MODE_BOX;
    if (color) memcpy(r.color, color, sizeof(r.color));
    if (border_color) memcpy(r.border_color, border_color, sizeof(r.border_color));
    sdui_draw_rect(&r);
}

void sdge_gui_shadow(sdge_gui *g, float x, float y, float w, float h,
                     float radius, float softness, const float color[4]) {
    (void)g;
    sdui_rect r;
    memset(&r, 0, sizeof(r));
    r.x = x; r.y = y; r.w = w; r.h = h;
    r.radius = radius;
    r.softness = softness;
    r.mode = SDUI_MODE_SOFT;
    if (color) memcpy(r.color, color, sizeof(r.color));
    sdui_draw_rect(&r);
}

void sdge_gui_text(sdge_gui *g, float x, float y, const char *text,
                   float size, const float color[4]) {
    (void)g;
    sdui_text(x, y, text, size, color);
}

void sdge_gui_text_center(sdge_gui *g, float x, float y, float w, float h,
                          const char *text, float size, const float color[4]) {
    (void)g;
    sdui_text_centered(x, y, w, h, text, size, color);
}
