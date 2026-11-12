/* scene.c - Silver Diamond Game Engine :: scene management implementation */
#include "scene.h"

#include <string.h>

#include "../subjects/2D/gui/gui.h"

static float g_fade_color[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

void sdge_scene_mgr_set_fade_color(sdge_scene_manager *m,
                                   const float rgba[4]) {
    (void)m;
    if (rgba) memcpy(g_fade_color, rgba, sizeof(g_fade_color));
}

void sdge_scene_mgr_init(sdge_scene_manager *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
    m->current = -1;
    m->next = -1;
    m->fade = 0.0f;
    m->fade_dir = 0;
    m->fade_speed = 0.45f;      /* one fade = 0.45 s */
    m->fade_delay = 0.0f;
}

int sdge_scene_mgr_add(sdge_scene_manager *m, sdge_scene *s) {
    if (!m || !s) return -1;
    if (m->count >= SDGE_SCENE_MAX) return -1;
    m->scenes[m->count++] = s;
    return m->count - 1;
}

static void scene_enter(sdge_scene_manager *m, int index) {
    if (!m || index < 0 || index >= m->count) return;
    sdge_scene *s = m->scenes[index];
    if (s && s->on_enter) s->on_enter(s);
}

static void scene_exit(sdge_scene_manager *m, int index) {
    if (!m || index < 0 || index >= m->count) return;
    sdge_scene *s = m->scenes[index];
    if (s && s->on_exit) s->on_exit(s);
}

void sdge_scene_mgr_switch_now(sdge_scene_manager *m, int index) {
    if (!m || index < 0 || index >= m->count) return;
    if (m->current == index && m->entered) return;
    if (m->entered) scene_exit(m, m->current);
    m->current = index;
    m->entered = 1;
    m->next = -1;
    m->fade = 0.0f;
    m->fade_dir = 0;
    scene_enter(m, index);
}

void sdge_scene_mgr_switch(sdge_scene_manager *m, int index) {
    if (!m || index < 0 || index >= m->count) return;
    if (m->current == index && m->entered) return;
    if (m->fade_dir != 0) return;          /* a transition is running */
    m->next = index;
    m->fade_dir = -1;                      /* fade out first */
}

void sdge_scene_mgr_update(sdge_scene_manager *m, float dt) {
    if (!m) return;
    if (dt < 0.0f) dt = 0.0f;

    if (m->fade_dir < 0) {
        /* fading out */
        m->fade += dt / (m->fade_speed > 0.001f ? m->fade_speed : 0.001f);
        if (m->fade >= 1.0f) {
            m->fade = 1.0f;
            if (m->next >= 0) {
                scene_exit(m, m->current);
                m->current = m->next;
                m->next = -1;
                scene_enter(m, m->current);
            }
            m->fade_dir = 1;               /* fade in */
        }
    } else if (m->fade_dir > 0) {
        /* fading in */
        m->fade -= dt / (m->fade_speed > 0.001f ? m->fade_speed : 0.001f);
        if (m->fade <= 0.0f) {
            m->fade = 0.0f;
            m->fade_dir = 0;
        }
    }

    if (m->current >= 0 && m->current < m->count) {
        sdge_scene *s = m->scenes[m->current];
        if (s && s->on_update) s->on_update(s, dt);
    }
}

void sdge_scene_mgr_render(sdge_scene_manager *m) {
    if (!m) return;
    if (m->current < 0 || m->current >= m->count) return;
    sdge_scene *s = m->scenes[m->current];
    if (s && s->on_render) s->on_render(s);
}

void sdge_scene_mgr_render_gui(sdge_scene_manager *m, struct sdge_gui *gui) {
    if (!m) return;
    if (m->current >= 0 && m->current < m->count) {
        sdge_scene *s = m->scenes[m->current];
        if (s && s->on_gui) s->on_gui(s, gui);
    }
    /* the transition veil is drawn by the UI shader as well */
    if (m->fade > 0.001f && gui) {
        float c[4];
        int w = 0, h = 0;
        memcpy(c, g_fade_color, sizeof(c));
        c[3] = g_fade_color[3] * m->fade;
        sdge_gui_viewport(gui, &w, &h);
        if (w > 0 && h > 0)
            sdge_gui_fill(gui, 0.0f, 0.0f, (float)w, (float)h, c);
    }
}

void sdge_scene_mgr_event(sdge_scene_manager *m, int type, int param) {
    if (!m) return;
    if (m->current < 0 || m->current >= m->count) return;
    sdge_scene *s = m->scenes[m->current];
    if (s && s->on_event) s->on_event(s, type, param);
}

sdge_scene *sdge_scene_mgr_current(sdge_scene_manager *m) {
    if (!m || m->current < 0 || m->current >= m->count) return NULL;
    return m->scenes[m->current];
}

int sdge_scene_mgr_transitioning(const sdge_scene_manager *m) {
    return m ? (m->fade_dir != 0) : 0;
}

float sdge_scene_mgr_fade(const sdge_scene_manager *m) {
    return m ? m->fade : 0.0f;
}
