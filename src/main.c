/* src/main.c - Silver Diamond Game Engine :: the showcase application.
 *
 * One window that demonstrates the whole engine:
 *
 *   - the scene manager with the fade transitions (5 scenes)
 *   - the shader GUI: div, button, input, label, checkbox, progress,
 *     slider, radio group and list box
 *   - the TrueType and the OpenType/CFF font loaders (F switches them)
 *   - the 3D world: a skinned glTF model with the animations, the sun,
 *     the ambient and the point lights
 *   - the 3D physics: spheres and boxes bouncing on the floor
 *   - the 2D physics: circles and boxes seen through the 2D camera
 *   - the audio: the spatial wav source and the mp3 music track
 *
 * Keys:
 *   1 / 2   play the animation "idle" / "wave"
 *   M       mute/unmute the audio
 *   P       restart the music track
 *   F       switch the UI font (TrueType <-> OpenType)
 *   SPACE   spawn the bodies in the physics scenes
 *   ESC     quit
 *
 * Automated run (for the tests):
 *   "silver_diamond --shot 30 [file.ppm]"  renders 30 frames and exits
 *   "silver_diamond --scene N"             starts in the scene N
 *   "silver_diamond --font otf|ttf"        picks the UI font
 *   "silver_diamond --drops N"             drops N groups of bodies at start
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <sdge.h>

/* ------------------------------------------------------------ app state */
typedef struct {
    sdge_window *win;
    int fbw, fbh;

    /* the camera (orbit) */
    sdge_camera3d cam;
    float cam_yaw, cam_pitch, cam_dist;
    float cam_center[3];

    /* input */
    double last_mx, last_my;
    int dragging;

    /* the 3D world */
    sdge_model *character;
    sdge_model *floor;
    int anim_index;
    char anim_name[64];
    sdge_sun sun;
    sdge_ambient_light ambient;
    sdge_point_light lamp;

    /* the audio */
    sdge_audio_buffer *beep;
    sdge_audio_source *beep_src;
    sdge_audio_buffer *music;
    sdge_audio_source *music_src;
    int muted;
    int audio_ok;

    /* the fonts */
    sdga_font *font_ttf;
    sdga_font *font_otf;
    int font_otf_active;

    /* the gui */
    sdge_gui *gui;
    sdge_scene_manager scenes;
    sdge_scene menu_scene, world_scene, phys3d_scene, phys2d_scene,
                credits_scene;

    /* the widgets */
    sdge_gui_div     menu_panel, world_panel, phys_panel, credits_panel;
    sdge_gui_button  btn_play, btn_phys, btn_phys2d, btn_credits, btn_quit;
    sdge_gui_button  btn_back;
    sdge_gui_button  btn_idle, btn_wave, btn_menu, btn_spawn, btn_reset;
    sdge_gui_input   name_input;
    sdge_gui_label   lbl_title, lbl_hint, lbl_fps, lbl_name, lbl_mode;
    sdge_gui_label   lbl_phys, lbl_credits;
    sdge_gui_checkbox chk_sound, chk_shadows;
    sdge_gui_progress prog_load;
    sdge_gui_slider  sld_zoom;
    sdge_gui_radio_group radio_group;
    sdge_gui_radio   radio_low, radio_mid, radio_high;
    sdge_gui_listbox list_scenes;

    /* the 2D camera */
    sdge_camera2d cam2d;

    /* the 3D physics */
    sdph3_world *phys3;
    sdge_mesh box_mesh, sphere_mesh;
    int phys3_count;

    /* the 2D physics */
    sdph2_world *phys2;
    int phys2_count;

    /* the fps */
    double last_time;
    double fps_accum;
    int fps_frames;
    int fps_display;
    char title[256];
} app;

static app g;

/* the palette */
static const float C_GOLD[4]    = { 0.93f, 0.78f, 0.42f, 1.0f };
static const float C_TEXT[4]    = { 0.90f, 0.92f, 0.96f, 1.0f };
static const float C_MUTED[4]   = { 0.58f, 0.62f, 0.70f, 1.0f };
static const float C_PANEL[4]   = { 0.09f, 0.10f, 0.14f, 0.90f };
static const float C_PANEL_B[4] = { 0.25f, 0.28f, 0.38f, 1.0f };
static const float C_SHADOW[4]  = { 0.0f, 0.0f, 0.0f, 0.45f };
static const float C_ACCENT[4]  = { 0.35f, 0.70f, 0.95f, 1.0f };

static void set_title(void) {
    snprintf(g.title, sizeof(g.title),
             "Silver Diamond Engine - %d fps, scene '%s', font %s%s%s",
             g.fps_display,
             sdge_scene_mgr_current(&g.scenes)
                 ? sdge_scene_mgr_current(&g.scenes)->name : "-",
             g.font_otf_active ? "OTF/CFF" : "TTF",
             g.muted ? ", muted" : "",
             !g.audio_ok ? ", no audio device" : "");
    sdge_set_window_title(g.win, g.title);
}

/* ----------------------------------------------------------- gui actions */
static void act_play(void *user) {
    (void)user;
    sdge_scene_mgr_switch(&g.scenes, 1);
}

static void act_phys3d(void *user) {
    (void)user;
    sdge_scene_mgr_switch(&g.scenes, 2);
}

static void act_phys2d(void *user) {
    (void)user;
    sdge_scene_mgr_switch(&g.scenes, 3);
}

static void act_credits(void *user) {
    (void)user;
    sdge_scene_mgr_switch(&g.scenes, 4);
}

static void act_back(void *user) {
    (void)user;
    sdge_scene_mgr_switch(&g.scenes, 0);
}

static void act_quit(void *user) {
    (void)user;
    sdge_set_window_should_close(g.win, true);
}

static void act_idle(void *user) {
    (void)user;
    if (g.character && sdge_model_anim_count(g.character) > 0) {
        sdge_model_play(g.character, 0, 1);
        g.anim_index = 0;
        snprintf(g.anim_name, sizeof(g.anim_name), "%s",
                 sdge_model_anim_name(g.character, 0));
    }
}

static void act_wave(void *user) {
    (void)user;
    if (g.character && sdge_model_anim_count(g.character) > 1) {
        sdge_model_play(g.character, 1, 1);
        g.anim_index = 1;
        snprintf(g.anim_name, sizeof(g.anim_name), "%s",
                 sdge_model_anim_name(g.character, 1));
    }
}

static void act_menu(void *user) {
    (void)user;
    sdge_scene_mgr_switch(&g.scenes, 0);
}

static void act_spawn(void *user) {
    (void)user;
    /* drop a few bodies into the physics scenes */
    for (int i = 0; i < 3; ++i) {
        if (sdge_scene_mgr_current(&g.scenes) == &g.phys3d_scene) {
            float x = (float)(rand() % 100 - 50) * 0.05f;
            float z = (float)(rand() % 100 - 50) * 0.05f;
            float y = 3.0f + (float)i * 0.7f;
            if (i % 2 == 0) {
                sdph3_body *b = sdph3_add_sphere(g.phys3, x, y, z, 0.3f, 1.0f);
                if (b) b->restitution = 0.55f;
            } else {
                sdph3_body *b = sdph3_add_box(g.phys3, x, y, z, 0.35f, 0.35f,
                                              0.35f, 1.0f);
                if (b) b->restitution = 0.3f;
            }
            g.phys3_count += 3;
        } else {
            float x = (float)(rand() % 100 - 50) * 0.05f;
            float y = 4.0f + (float)i * 0.6f;
            if (i % 2 == 0) {
                sdph2_body *b = sdph2_add_circle(g.phys2, x, y, 0.32f, 1.0f);
                if (b) b->restitution = 0.5f;
            } else {
                sdph2_body *b = sdph2_add_box(g.phys2, x, y, 0.35f, 0.35f, 1.0f,
                                              0.0f);
                if (b) b->restitution = 0.3f;
            }
            g.phys2_count += 3;
        }
    }
}

static void act_reset(void *user) {
    (void)user;
    if (sdge_scene_mgr_current(&g.scenes) == &g.phys3d_scene) {
        while (g.phys3 && g.phys3->count > 0) {
            sdph3_remove(g.phys3, &g.phys3->bodies[0]);
            g.phys3_count--;
        }
    } else {
        while (g.phys2 && g.phys2->count > 0) {
            sdph2_remove(g.phys2, &g.phys2->bodies[0]);
            g.phys2_count--;
        }
    }
}

static void on_name_changed(void *user, const char *text) {
    (void)user;
    (void)text;
}

static void on_sound_changed(void *user, int checked) {
    (void)user;
    g.muted = !checked;
    sdge_audio_set_master_volume(checked ? 1.0f : 0.0f);
}

static void on_shadows_changed(void *user, int checked) {
    (void)user;
    (void)checked;
}

static void on_zoom_changed(void *user, float value) {
    (void)user;
    g.cam_dist = 2.0f + value * 10.0f;
}

static void on_quality_changed(void *user, int index) {
    (void)user;
    (void)index;
}

static void on_scene_picked(void *user, int index) {
    (void)user;
    if (index >= 0 && index < 5)
        sdge_scene_mgr_switch(&g.scenes, index);
}

/* -------------------------------------------------------------- callbacks */
static void on_key(sdge_window *w, int key, int sc, int action, int mods) {
    (void)w; (void)sc; (void)mods;
    if (action != SDGE_PRESS) return;

    if (key == SDGE_KEY_ESCAPE) {
        sdge_set_window_should_close(g.win, true);
        return;
    }
    if (key == SDGE_KEY_1) { act_idle(NULL); return; }
    if (key == SDGE_KEY_2) { act_wave(NULL); return; }
    if (key == SDGE_KEY_M) {
        g.muted = !g.muted;
        sdge_audio_set_master_volume(g.muted ? 0.0f : 1.0f);
        sdge_gui_checkbox_set(&g.chk_sound, g.muted ? 0 : 1);
        set_title();
        return;
    }
    if (key == SDGE_KEY_P) {
        if (g.music_src) {
            sdge_audio_source_stop(g.music_src);
            sdge_audio_source_play(g.music_src);
        }
        return;
    }
    if (key == SDGE_KEY_F) {
        if (g.font_ttf && g.font_otf) {
            g.font_otf_active = !g.font_otf_active;
            sdui_set_font(g.font_otf_active ? g.font_otf : g.font_ttf);
        }
        return;
    }
    if (key == SDGE_KEY_SPACE) { act_spawn(NULL); return; }
    if (g.gui) sdge_gui_key(g.gui, key, action);
    sdge_scene_mgr_event(&g.scenes, SDGE_SCENE_EVENT_KEY, key);
}

static void on_char(sdge_window *w, unsigned codepoint) {
    (void)w;
    if (g.gui) sdge_gui_char(g.gui, codepoint);
}

static void on_button(sdge_window *w, int button, int action, int mods) {
    (void)w; (void)mods;
    if (g.gui) sdge_gui_mouse_button(g.gui, button, action);
    if (button == SDGE_MOUSE_BUTTON_LEFT && action == SDGE_PRESS) {
        if (!g.gui || sdge_gui_hovered(g.gui) < 0) {
            g.dragging = 1;
            sdge_get_cursor_pos(g.win, &g.last_mx, &g.last_my);
        } else {
            g.dragging = 0;
        }
    } else if (button == SDGE_MOUSE_BUTTON_LEFT && action == SDGE_RELEASE) {
        g.dragging = 0;
    }
}

static void on_cursor(sdge_window *w, double x, double y) {
    (void)w;
    if (g.gui) sdge_gui_mouse_move(g.gui, (float)x, (float)y);
    if (!g.dragging) return;
    g.cam_yaw   -= (float)(x - g.last_mx) * 0.008f;
    g.cam_pitch += (float)(y - g.last_my) * 0.008f;
    if (g.cam_pitch > 1.45f) g.cam_pitch = 1.45f;
    if (g.cam_pitch < 0.05f) g.cam_pitch = 0.05f;
    g.last_mx = x;
    g.last_my = y;
}

static void on_scroll(sdge_window *w, double dx, double dy) {
    (void)w; (void)dx;
    if (g.gui) sdge_gui_scroll(g.gui, (float)dy);
    g.cam_dist *= (dy > 0) ? 0.9f : 1.1f;
    if (g.cam_dist < 1.0f)  g.cam_dist = 1.0f;
    if (g.cam_dist > 15.0f) g.cam_dist = 15.0f;
}

static void on_size(sdge_window *w, int width, int height) {
    (void)w;
    g.fbw = width;
    g.fbh = height;
}

static void on_error(int code, const char *desc) {
    fprintf(stderr, "engine error %d: %s\n", code, desc);
}

/* ----------------------------------------------------------------- scenes */
static void show_only_menu(void) {
    /* hides everything - every scene shows what it needs */
    g.menu_panel.base.visible = 0;
    g.world_panel.base.visible = 0;
    g.phys_panel.base.visible = 0;
    g.credits_panel.base.visible = 0;
    g.btn_play.base.visible = 0;
    g.btn_phys.base.visible = 0;
    g.btn_phys2d.base.visible = 0;
    g.btn_credits.base.visible = 0;
    g.btn_quit.base.visible = 0;
    g.btn_back.base.visible = 0;
    g.name_input.base.visible = 0;
    g.list_scenes.base.visible = 0;
    g.lbl_title.base.visible = 0;
    g.lbl_hint.base.visible = 0;
    g.lbl_fps.base.visible = 0;
    g.lbl_name.base.visible = 0;
    g.lbl_mode.base.visible = 0;
    g.lbl_phys.base.visible = 0;
    g.lbl_credits.base.visible = 0;
    g.chk_sound.base.visible = 0;
    g.chk_shadows.base.visible = 0;
    g.prog_load.base.visible = 0;
    g.sld_zoom.base.visible = 0;
    g.radio_low.base.visible = 0;
    g.radio_mid.base.visible = 0;
    g.radio_high.base.visible = 0;
    g.btn_idle.base.visible = 0;
    g.btn_wave.base.visible = 0;
    g.btn_menu.base.visible = 0;
    g.btn_spawn.base.visible = 0;
    g.btn_reset.base.visible = 0;
}

static void menu_on_enter(sdge_scene *s) {
    (void)s;
    show_only_menu();
    g.menu_panel.base.visible = 1;
    g.btn_play.base.visible = 1;
    g.btn_phys.base.visible = 1;
    g.btn_phys2d.base.visible = 1;
    g.btn_credits.base.visible = 1;
    g.btn_quit.base.visible = 1;
    g.name_input.base.visible = 1;
    g.list_scenes.base.visible = 1;
    g.chk_sound.base.visible = 1;
    g.chk_shadows.base.visible = 1;
    g.prog_load.base.visible = 1;
    g.sld_zoom.base.visible = 1;
    g.radio_low.base.visible = 1;
    g.radio_mid.base.visible = 1;
    g.radio_high.base.visible = 1;
}

static void menu_on_gui(sdge_scene *s, sdge_gui *gui) {
    (void)s;
    float px = g.menu_panel.base.x, py = g.menu_panel.base.y;
    float pw = g.menu_panel.base.w;
    sdge_gui_text_center(gui, px, py + 16.0f, pw, 40.0f, "SILVER DIAMOND", 30.0f,
                         C_GOLD);
    sdge_gui_text_center(gui, px, py + 50.0f, pw, 22.0f,
                         "the engine showcase", 14.0f, C_MUTED);
    sdge_gui_text_center(gui, g.lbl_mode.base.x, 372.0f, 200.0f, 20.0f,
                         "1/2 anim  M mute  P music  F font", 11.0f, C_MUTED);
}

static void world_on_enter(sdge_scene *s) {
    (void)s;
    show_only_menu();
    g.world_panel.base.visible = 1;
    g.btn_idle.base.visible = 1;
    g.btn_wave.base.visible = 1;
    g.btn_menu.base.visible = 1;
    g.lbl_fps.base.visible = 1;
    g.lbl_fps.base.x = 296.0f;
    g.lbl_fps.base.y = 24.0f;
    g.lbl_fps.base.w = 160.0f;
    sdge_gui_label_set_text(&g.lbl_fps, "the world scene");
    /* frame the skinned character (it is about 1.8 units tall) */
    g.cam_center[0] = 0.0f; g.cam_center[1] = 0.9f; g.cam_center[2] = 0.0f;
    g.cam_yaw = 0.55f; g.cam_pitch = 0.28f; g.cam_dist = 2.6f;
}

static void world_on_update(sdge_scene *s, float dt) {
    (void)s; (void)dt;
}

static void world_on_render(sdge_scene *s) {
    (void)s;
    if (g.floor)     sdge_model_draw(g.floor, NULL);
    if (g.character) sdge_model_draw(g.character, NULL);
}

static void world_on_gui(sdge_scene *s, sdge_gui *gui) {
    (void)s;
    float px = g.world_panel.base.x, py = g.world_panel.base.y;
    char line[128];
    sdge_gui_text(gui, px + 16.0f, py + 12.0f, "WORLD", 16.0f, C_GOLD);
    snprintf(line, sizeof(line), "fps      %d", g.fps_display);
    sdge_gui_text(gui, px + 16.0f, py + 40.0f, line, 13.0f, C_TEXT);
    snprintf(line, sizeof(line), "anim     %s",
             g.anim_index >= 0 ? g.anim_name : "bind pose");
    sdge_gui_text(gui, px + 16.0f, py + 60.0f, line, 13.0f, C_TEXT);
    snprintf(line, sizeof(line), "music    %s",
             g.music_src && sdge_audio_source_playing(g.music_src)
                 ? "playing" : "stopped");
    sdge_gui_text(gui, px + 16.0f, py + 80.0f, line, 13.0f, C_TEXT);
    snprintf(line, sizeof(line), "font     %s",
             g.font_otf_active ? "OTF/CFF" : "TrueType");
    sdge_gui_text(gui, px + 16.0f, py + 100.0f, line, 13.0f, C_TEXT);
}

static void phys3d_on_enter(sdge_scene *s) {
    (void)s;
    show_only_menu();
    g.phys_panel.base.visible = 1;
    g.btn_spawn.base.visible = 1;
    g.btn_reset.base.visible = 1;
    g.btn_menu.base.visible = 1;
    g.cam_dist = 9.0f;
    if (g.phys3_count == 0) act_spawn(NULL);
}

static void phys3d_on_update(sdge_scene *s, float dt) {
    (void)s;
    if (g.phys3) sdph3_step(g.phys3, dt);
}

static void phys3d_on_render(sdge_scene *s) {
    (void)s;
    if (!g.phys3) return;
    /* the floor and the bodies */
    for (int i = 0; i < g.phys3->count; ++i) {
        const sdph3_body *b = &g.phys3->bodies[i];
        float col[4] = { 0.85f, 0.72f, 0.45f, 1.0f };
        if (b->shape == SDPH3_SPHERE) col[1] = 0.45f;
        if (b->inv_mass <= 0.0f) {
            /* the static floor: a darker slab */
            col[0] = 0.16f; col[1] = 0.18f; col[2] = 0.24f;
        }

        /* the model matrix: T * R * S, with the mesh built as a 0.35 box and
         * a 0.3 sphere */
        float cx = cosf(b->angle[0]), sx = sinf(b->angle[0]);
        float cy = cosf(b->angle[1]), sy = sinf(b->angle[1]);
        float cz = cosf(b->angle[2]), sz = sinf(b->angle[2]);
        float r[9];
        r[0] = cz * cy; r[1] = cz * sy * sx - sz * cx; r[2] = cz * sy * cx + sz * sx;
        r[3] = sz * cy; r[4] = sz * sy * sx + cz * cx; r[5] = sz * sy * cx - cz * sx;
        r[6] = -sy;     r[7] = cy * sx;                r[8] = cy * cx;
        float sc[3];
        if (b->shape == SDPH3_SPHERE) {
            float k = b->radius / 0.3f;
            sc[0] = sc[1] = sc[2] = k;
        } else {
            sc[0] = b->half[0] / 0.35f;
            sc[1] = b->half[1] / 0.35f;
            sc[2] = b->half[2] / 0.35f;
        }
        float m[16];
        /* column c of R*S is the c-th column of R times sc[c] */
        m[0] = r[0] * sc[0]; m[1] = r[3] * sc[0]; m[2] = r[6] * sc[0]; m[3] = 0;
        m[4] = r[1] * sc[1]; m[5] = r[4] * sc[1]; m[6] = r[7] * sc[1]; m[7] = 0;
        m[8] = r[2] * sc[2]; m[9] = r[5] * sc[2]; m[10] = r[8] * sc[2]; m[11] = 0;
        m[12] = b->pos[0]; m[13] = b->pos[1]; m[14] = b->pos[2]; m[15] = 1;

        const sdge_mesh *mesh = (b->shape == SDPH3_SPHERE) ? &g.sphere_mesh
                                                           : &g.box_mesh;
        sdgr_draw_mesh(mesh, m, NULL, 0, col, 0.1f, 0.5f, 0);
    }
}

static void phys3d_on_gui(sdge_scene *s, sdge_gui *gui) {
    (void)s;
    float px = g.phys_panel.base.x, py = g.phys_panel.base.y;
    char line[128];
    sdge_gui_text(gui, px + 16.0f, py + 12.0f, "3D PHYSICS", 16.0f, C_GOLD);
    snprintf(line, sizeof(line), "bodies   %d", g.phys3 ? g.phys3->count : 0);
    sdge_gui_text(gui, px + 16.0f, py + 40.0f, line, 13.0f, C_TEXT);
    snprintf(line, sizeof(line), "contacts %d", g.phys3 ? g.phys3->contacts : 0);
    sdge_gui_text(gui, px + 16.0f, py + 60.0f, line, 13.0f, C_TEXT);
    sdge_gui_text(gui, px + 16.0f, py + 80.0f, "SPACE drops the bodies", 12.0f,
                  C_MUTED);
}

static void phys2d_on_enter(sdge_scene *s) {
    (void)s;
    show_only_menu();
    g.phys_panel.base.visible = 1;
    g.btn_spawn.base.visible = 1;
    g.btn_reset.base.visible = 1;
    g.btn_menu.base.visible = 1;
    if (g.phys2_count == 0) act_spawn(NULL);
}

static void phys2d_on_update(sdge_scene *s, float dt) {
    (void)s;
    if (g.phys2) sdph2_step(g.phys2, dt);
}

static void phys2d_on_gui(sdge_scene *s, sdge_gui *gui) {
    (void)s;
    /* the bodies are drawn through the UI shader (a top view) */
    if (g.phys2) {
        float z = g.cam2d.zoom;
        for (int i = 0; i < g.phys2->count; ++i) {
            const sdph2_body *b = &g.phys2->bodies[i];
            float scr[2];
            sdge_camera2d_world_to_screen(&g.cam2d, b->pos[0], b->pos[1], scr);
            float col[4] = { 0.85f, 0.72f, 0.45f, 1.0f };
            if (b->shape == SDPH2_CIRCLE) {
                col[1] = 0.45f;
                float r = b->radius * z;
                sdge_gui_round(gui, scr[0] - r, scr[1] - r, r * 2.0f, r * 2.0f,
                               r, col);
            } else {
                float hw = b->half[0] * z, hh = b->half[1] * z;
                sdge_gui_round(gui, scr[0] - hw, scr[1] - hh, hw * 2.0f, hh * 2.0f,
                               3.0f, col);
            }
        }
    }
    float px = g.phys_panel.base.x, py = g.phys_panel.base.y;
    char line[128];
    sdge_gui_text(gui, px + 16.0f, py + 12.0f, "2D PHYSICS", 16.0f, C_GOLD);
    snprintf(line, sizeof(line), "bodies   %d", g.phys2 ? g.phys2->count : 0);
    sdge_gui_text(gui, px + 16.0f, py + 40.0f, line, 13.0f, C_TEXT);
    snprintf(line, sizeof(line), "contacts %d", g.phys2 ? g.phys2->contacts : 0);
    sdge_gui_text(gui, px + 16.0f, py + 60.0f, line, 13.0f, C_TEXT);
    sdge_gui_text(gui, px + 16.0f, py + 80.0f, "the 2D camera draws them", 12.0f,
                  C_MUTED);
}

static void credits_on_enter(sdge_scene *s) {
    (void)s;
    show_only_menu();
    g.credits_panel.base.visible = 1;
    g.btn_back.base.visible = 1;
}

static void credits_on_gui(sdge_scene *s, sdge_gui *gui) {
    (void)s;
    float px = g.credits_panel.base.x, py = g.credits_panel.base.y;
    float pw = g.credits_panel.base.w;
    sdge_gui_text_center(gui, px, py + 24.0f, pw, 34.0f, "CREDITS", 24.0f, C_GOLD);
    const char *lines[7] = {
        "Silver Diamond Game Engine 0.3",
        "renderer   - forward PBR-lite, GL 3.3+",
        "gui        - one shader: SDF shapes + a font atlas",
        "fonts      - TrueType (glyf) and OpenType/CFF",
        "physics    - 2D and 3D rigid bodies, impulses",
        "audio      - 48 kHz mixer, wav + mp3",
        "assets     - glTF 2.0, animation and skinning"
    };
    for (int i = 0; i < 7; ++i)
        sdge_gui_text(gui, px + 40.0f, py + 78.0f + (float)i * 22.0f, lines[i],
                      13.0f, i == 0 ? C_TEXT : C_MUTED);
}

/* ------------------------------------------------------------------ main */
int main(int argc, char **argv) {
    memset(&g, 0, sizeof(g));

    int shot_frame = 0;
    const char *shot_path = "demo_shot.ppm";
    int start_scene = 0;
    int want_otf = 0;
    int auto_drops = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot_frame = atoi(argv[++i]);
            if (shot_frame < 1) shot_frame = 1;
            if (i + 1 < argc && argv[i + 1][0] != '-') shot_path = argv[++i];
        } else if (strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            start_scene = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--font") == 0 && i + 1 < argc) {
            want_otf = (strstr(argv[++i], "otf") != NULL);
        } else if (strcmp(argv[i], "--drops") == 0 && i + 1 < argc) {
            auto_drops = atoi(argv[++i]);
            if (auto_drops < 0) auto_drops = 0;
            if (auto_drops > 100) auto_drops = 100;
        }
    }
    int frame_no = 0;

    if (!sdge_init()) {
        fprintf(stderr, "engine init failed\n");
        return 1;
    }
    sdge_set_error_callback(on_error);

    sdge_window_hint(SDGE_VSYNC, 1);
    g.win = sdge_create_window(1280, 720, "Silver Diamond Engine", NULL);
    if (!g.win) {
        fprintf(stderr, "window creation failed\n");
        sdge_terminate();
        return 1;
    }

    sdge_set_key_callback(g.win, on_key);
    sdge_set_char_callback(g.win, on_char);
    sdge_set_mouse_button_callback(g.win, on_button);
    sdge_set_cursor_pos_callback(g.win, on_cursor);
    sdge_set_scroll_callback(g.win, on_scroll);
    sdge_set_window_size_callback(g.win, on_size);

    sdge_get_framebuffer_size(g.win, &g.fbw, &g.fbh);
    sdge_show_window(g.win);
    sdge_make_context_current(g.win);

    int gl_ok = (sdgr_init() == 0);
    int ui_ok = (sdui_init() == 0);
    if (!ui_ok) fprintf(stderr, "demo: the UI renderer is not available\n");

    /* the fonts: a TTF and an OpenType/CFF one, rasterized at every size
     * the UI uses (so the text is never scaled down) */
    if (ui_ok) {
        g.font_ttf = sdga_load_font("assets/font.ttf", 30.0f);
        g.font_otf = sdga_load_font("assets/font.otf", 30.0f);
        static const float sizes[] = { 12.0f, 13.0f, 14.0f, 16.0f, 18.0f,
                                      22.0f, 30.0f };
        for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
            if (g.font_ttf) sdga_font_add_face(g.font_ttf, sizes[i]);
            if (g.font_otf) sdga_font_add_face(g.font_otf, sizes[i]);
        }
        if (g.font_ttf) {
            sdui_set_font(want_otf && g.font_otf ? g.font_otf : g.font_ttf);
            g.font_otf_active = (want_otf && g.font_otf) ? 1 : 0;
            fprintf(stderr, "demo: font '%s' (%d glyphs, %d sizes)\n",
                    sdga_font_name(sdui_get_font()),
                    sdga_font_glyph_count(g.font_ttf),
                    sdga_font_face_count(g.font_ttf));
        }
        if (g.font_otf)
            fprintf(stderr, "demo: font '%s' (%d glyphs, %d sizes)\n",
                    sdga_font_name(g.font_otf),
                    sdga_font_glyph_count(g.font_otf),
                    sdga_font_face_count(g.font_otf));
    }

    /* the 3D world */
    g.cam_center[0] = 0.0f; g.cam_center[1] = 1.0f; g.cam_center[2] = 0.0f;
    g.cam_yaw = 0.6f; g.cam_pitch = 0.35f; g.cam_dist = 4.0f;
    sdge_camera3d_init(&g.cam, 50.0f, (float)g.fbw / (float)g.fbh, 0.1f, 100.0f);
    sdge_sun_init(&g.sun, NULL, NULL, 2.2f);
    sdge_ambient_light_init(&g.ambient, NULL);
    {
        float pos[3] = { 2.0f, 3.0f, 2.0f };
        float col[3] = { 1.0f, 0.75f, 0.45f };
        sdge_point_light_init(&g.lamp, pos, col, 1.4f, 9.0f);
    }

    if (gl_ok) {
        /* the skinned character with its two animation clips; the huge
         * "scene.glb" scan has no animations and the camera would sit
         * inside it, so the showcase uses the character */
        g.character = sdge_model_load("Subjects/Scene.glb");
        if (g.character && sdge_model_anim_count(g.character) > 0) {
            sdge_model_play(g.character, 0, 1);
            g.anim_index = 0;
            snprintf(g.anim_name, sizeof(g.anim_name), "%s",
                     sdge_model_anim_name(g.character, 0));
        }
        g.floor = sdge_model_load("Subjects/plane.glb");
        if (gl_ok) {
            sdge_mesh_create_box(&g.box_mesh, 0.35f, 0.35f, 0.35f);
            sdge_mesh_create_sphere(&g.sphere_mesh, 0.3f, 20, 12);
        }
    }

    /* the physics worlds */
    g.phys3 = sdph3_world_create();
    if (g.phys3) {
        sdph3_world_set_gravity(g.phys3, 0.0f, -9.81f, 0.0f);
        sdph3_world_set_bounds(g.phys3, 1, -6.0f, -1.0f, -6.0f,
                               6.0f, 12.0f, 6.0f);
        sdph3_add_box(g.phys3, 0.0f, -0.5f, 0.0f, 6.0f, 0.5f, 6.0f, 0.0f);
    }
    g.phys2 = sdph2_world_create();
    if (g.phys2) {
        sdph2_world_set_gravity(g.phys2, 0.0f, -9.81f);
        sdph2_world_set_bounds(g.phys2, 1, -8.0f, -8.0f, 8.0f, 8.0f);
        sdph2_add_box(g.phys2, 0.0f, -8.5f, 8.5f, 0.5f, 0.0f, 0.0f);
    }
    sdge_camera2d_init(&g.cam2d, g.fbw, g.fbh);
    sdge_camera2d_set_zoom(&g.cam2d, 18.0f);

    /* the audio */
    sdge_audio_init();
    g.audio_ok = sdge_audio_has_device();
    g.beep = sdge_audio_load_wav("Subjects/beep.wav");
    if (g.beep) {
        g.beep_src = sdge_audio_source_create(g.beep);
        if (g.beep_src) {
            sdge_audio_source_set_loop(g.beep_src, 1);
            sdge_audio_source_set_distance(g.beep_src, 1.0f, 30.0f);
            /* the ping is a background effect - keep it quiet */
            sdge_audio_source_set_volume(g.beep_src, 0.18f);
            sdge_audio_source_play(g.beep_src);
        }
    }
    g.music = sdge_audio_load_file("assets/Detective(2).mp3");
    if (g.music) {
        g.music_src = sdge_audio_source_create(g.music);
        if (g.music_src) {
            sdge_audio_source_set_loop(g.music_src, 1);
            sdge_audio_source_set_volume(g.music_src, 0.6f);
            sdge_audio_source_play(g.music_src);
            fprintf(stderr, "demo: music '%s' - %d ch, %d Hz, %.1f s\n",
                    "assets/Detective(2).mp3", g.music->channels,
                    g.music->sample_rate,
                    (double)g.music->frame_count / (double)g.music->sample_rate);
        }
    }

    /* ------------------------------------------------------------ the gui */
    g.gui = sdge_gui_create();
    if (g.gui) {
        float mw = (float)g.fbw * 0.5f;
        float fx = (float)g.fbw - 300.0f;

        sdge_gui_div_init(&g.menu_panel, mw - 230.0f, 60.0f, 460.0f, 340.0f,
                          C_PANEL);
        g.menu_panel.border_color[0] = C_PANEL_B[0];
        g.menu_panel.border_color[1] = C_PANEL_B[1];
        g.menu_panel.border_color[2] = C_PANEL_B[2];
        g.menu_panel.border_color[3] = C_PANEL_B[3];
        g.menu_panel.border_width = 1.0f;
        g.menu_panel.shadow_color[3] = C_SHADOW[3];

        sdge_gui_div_init(&g.world_panel, 20.0f, 20.0f, 260.0f, 132.0f, C_PANEL);
        g.world_panel.border_color[3] = C_PANEL_B[3];
        g.world_panel.border_width = 1.0f;
        g.world_panel.radius = 10.0f;

        sdge_gui_div_init(&g.phys_panel, 20.0f, 20.0f, 260.0f, 132.0f, C_PANEL);
        g.phys_panel.border_color[3] = C_PANEL_B[3];
        g.phys_panel.border_width = 1.0f;
        g.phys_panel.radius = 10.0f;

        sdge_gui_div_init(&g.credits_panel, mw - 230.0f, 80.0f, 460.0f, 300.0f,
                          C_PANEL);
        g.credits_panel.border_color[3] = C_PANEL_B[3];
        g.credits_panel.border_width = 1.0f;

        /* the buttons */
        sdge_gui_button_init(&g.btn_play, mw - 90.0f, 186.0f, 180.0f, 38.0f,
                             "WORLD", act_play, NULL);
        sdge_gui_button_init(&g.btn_phys, mw - 90.0f, 232.0f, 180.0f, 38.0f,
                             "PHYSICS 3D", act_phys3d, NULL);
        sdge_gui_button_init(&g.btn_phys2d, mw - 90.0f, 278.0f, 180.0f, 34.0f,
                             "PHYSICS 2D", act_phys2d, NULL);
        sdge_gui_button_init(&g.btn_credits, mw - 90.0f, 320.0f, 180.0f, 34.0f,
                             "CREDITS", act_credits, NULL);
        sdge_gui_button_init(&g.btn_quit, mw - 90.0f, 358.0f, 180.0f, 34.0f,
                             "QUIT", act_quit, NULL);
        sdge_gui_button_init(&g.btn_back, mw - 80.0f, 400.0f, 160.0f, 36.0f,
                             "BACK", act_back, NULL);
        sdge_gui_button_init(&g.btn_idle, 20.0f, (float)g.fbh - 56.0f, 110.0f,
                             38.0f, "IDLE", act_idle, NULL);
        sdge_gui_button_init(&g.btn_wave, 140.0f, (float)g.fbh - 56.0f, 110.0f,
                             38.0f, "WAVE", act_wave, NULL);
        sdge_gui_button_init(&g.btn_menu, 260.0f, (float)g.fbh - 56.0f, 110.0f,
                             38.0f, "MENU", act_menu, NULL);
        sdge_gui_button_init(&g.btn_spawn, 380.0f, (float)g.fbh - 56.0f, 110.0f,
                             38.0f, "SPAWN", act_spawn, NULL);
        sdge_gui_button_init(&g.btn_reset, 500.0f, (float)g.fbh - 56.0f, 110.0f,
                             38.0f, "RESET", act_reset, NULL);

        /* the labels */
        sdge_gui_label_init(&g.lbl_title, mw - 200.0f, 68.0f, 400.0f, 30.0f,
                            "SILVER DIAMOND", 22.0f, C_GOLD);
        sdge_gui_label_init(&g.lbl_hint, mw - 200.0f, 372.0f, 400.0f, 20.0f,
                            "1/2 anim  M mute  P music  F font  SPACE spawn",
                            11.0f, C_MUTED);
        sdge_gui_label_init(&g.lbl_fps, 296.0f, 24.0f, 200.0f, 20.0f, "fps",
                            13.0f, C_TEXT);
        sdge_gui_label_init(&g.lbl_name, mw - 90.0f, 76.0f, 180.0f, 18.0f,
                            "pilot name", 13.0f, C_MUTED);
        sdge_gui_label_init(&g.lbl_mode, fx, 76.0f, 200.0f, 18.0f, "options",
                            13.0f, C_MUTED);
        sdge_gui_label_init(&g.lbl_phys, 296.0f, 24.0f, 240.0f, 20.0f, "physics",
                            13.0f, C_TEXT);
        sdge_gui_label_init(&g.lbl_credits, 296.0f, 24.0f, 200.0f, 20.0f,
                            "credits", 13.0f, C_TEXT);

        /* the input */
        sdge_gui_input_init(&g.name_input, mw - 90.0f, 140.0f, 180.0f, 30.0f,
                            "pilot name");
        g.name_input.on_change = on_name_changed;

        /* the checkbox */
        sdge_gui_checkbox_init(&g.chk_sound, fx, 96.0f, 16.0f, "sound", 1);
        g.chk_sound.on_change = on_sound_changed;
        sdge_gui_checkbox_init(&g.chk_shadows, fx, 122.0f, 16.0f, "shadows", 1);
        g.chk_shadows.on_change = on_shadows_changed;

        /* the progress bar */
        sdge_gui_progress_init(&g.prog_load, fx, 152.0f, 200.0f, 10.0f);
        sdge_gui_progress_set(&g.prog_load, 0.65f);

        /* the slider */
        sdge_gui_slider_init(&g.sld_zoom, fx, 180.0f, 200.0f, 22.0f, 0.2f);
        g.sld_zoom.on_change = on_zoom_changed;
        g.sld_zoom.fill_color[0] = C_ACCENT[0];
        g.sld_zoom.fill_color[1] = C_ACCENT[1];
        g.sld_zoom.fill_color[2] = C_ACCENT[2];
        g.sld_zoom.fill_color[3] = C_ACCENT[3];

        /* the radio group */
        sdge_gui_radio_group_init(&g.radio_group);
        g.radio_group.on_change = on_quality_changed;
        sdge_gui_radio_init(&g.radio_low, fx, 218.0f, 16.0f, 0, &g.radio_group);
        sdge_gui_radio_init(&g.radio_mid, fx + 66.0f, 218.0f, 16.0f, 1,
                            &g.radio_group);
        sdge_gui_radio_init(&g.radio_high, fx + 136.0f, 218.0f, 16.0f, 2,
                            &g.radio_group);
        strcpy(g.radio_low.label, "low");
        strcpy(g.radio_mid.label, "mid");
        strcpy(g.radio_high.label, "high");

        /* the list box */
        sdge_gui_listbox_init(&g.list_scenes, fx, 252.0f, 200.0f, 110.0f);
        sdge_gui_listbox_add(&g.list_scenes, "menu");
        sdge_gui_listbox_add(&g.list_scenes, "world");
        sdge_gui_listbox_add(&g.list_scenes, "physics 3d");
        sdge_gui_listbox_add(&g.list_scenes, "physics 2d");
        sdge_gui_listbox_add(&g.list_scenes, "credits");
        g.list_scenes.on_select = on_scene_picked;

        /* register everything */
        sdge_gui_add(g.gui, &g.menu_panel.base);
        sdge_gui_add(g.gui, &g.world_panel.base);
        sdge_gui_add(g.gui, &g.phys_panel.base);
        sdge_gui_add(g.gui, &g.credits_panel.base);
        sdge_gui_add(g.gui, &g.name_input.base);
        sdge_gui_add(g.gui, &g.btn_play.base);
        sdge_gui_add(g.gui, &g.btn_phys.base);
        sdge_gui_add(g.gui, &g.btn_phys2d.base);
        sdge_gui_add(g.gui, &g.btn_credits.base);
        sdge_gui_add(g.gui, &g.btn_quit.base);
        sdge_gui_add(g.gui, &g.btn_back.base);
        sdge_gui_add(g.gui, &g.btn_idle.base);
        sdge_gui_add(g.gui, &g.btn_wave.base);
        sdge_gui_add(g.gui, &g.btn_menu.base);
        sdge_gui_add(g.gui, &g.btn_spawn.base);
        sdge_gui_add(g.gui, &g.btn_reset.base);
        sdge_gui_add(g.gui, &g.chk_sound.base);
        sdge_gui_add(g.gui, &g.chk_shadows.base);
        sdge_gui_add(g.gui, &g.prog_load.base);
        sdge_gui_add(g.gui, &g.sld_zoom.base);
        sdge_gui_add(g.gui, &g.radio_low.base);
        sdge_gui_add(g.gui, &g.radio_mid.base);
        sdge_gui_add(g.gui, &g.radio_high.base);
        sdge_gui_add(g.gui, &g.list_scenes.base);
        sdge_gui_add(g.gui, &g.lbl_title.base);
        sdge_gui_add(g.gui, &g.lbl_hint.base);
        sdge_gui_add(g.gui, &g.lbl_fps.base);
        sdge_gui_add(g.gui, &g.lbl_name.base);
        sdge_gui_add(g.gui, &g.lbl_mode.base);
        sdge_gui_add(g.gui, &g.lbl_phys.base);
        sdge_gui_add(g.gui, &g.lbl_credits.base);
    }

    /* --------------------------------------------------------- the scenes */
    g.menu_scene.name = "menu";
    g.menu_scene.on_enter = menu_on_enter;
    g.menu_scene.on_gui = menu_on_gui;

    g.world_scene.name = "world";
    g.world_scene.on_enter = world_on_enter;
    g.world_scene.on_update = world_on_update;
    g.world_scene.on_render = world_on_render;
    g.world_scene.on_gui = world_on_gui;

    g.phys3d_scene.name = "physics3d";
    g.phys3d_scene.on_enter = phys3d_on_enter;
    g.phys3d_scene.on_update = phys3d_on_update;
    g.phys3d_scene.on_render = phys3d_on_render;
    g.phys3d_scene.on_gui = phys3d_on_gui;

    g.phys2d_scene.name = "physics2d";
    g.phys2d_scene.on_enter = phys2d_on_enter;
    g.phys2d_scene.on_update = phys2d_on_update;
    g.phys2d_scene.on_gui = phys2d_on_gui;

    g.credits_scene.name = "credits";
    g.credits_scene.on_enter = credits_on_enter;
    g.credits_scene.on_gui = credits_on_gui;

    sdge_scene_mgr_init(&g.scenes);
    sdge_scene_mgr_add(&g.scenes, &g.menu_scene);
    sdge_scene_mgr_add(&g.scenes, &g.world_scene);
    sdge_scene_mgr_add(&g.scenes, &g.phys3d_scene);
    sdge_scene_mgr_add(&g.scenes, &g.phys2d_scene);
    sdge_scene_mgr_add(&g.scenes, &g.credits_scene);
    if (start_scene < 0 || start_scene > 4) start_scene = 0;
    sdge_scene_mgr_switch_now(&g.scenes, start_scene);
    for (int d = 0; d < auto_drops; ++d) act_spawn(NULL);

    set_title();
    g.last_time = sdge_get_time();

    /* ------------------------------------------------------- the main loop */
    while (!sdge_window_should_close(g.win)) {
        sdge_poll_events();

        double now = sdge_get_time();
        /* in the screenshot mode the loop runs as fast as it can, so the real
         * delta would be a fraction of a millisecond; use a fixed step there
         * to make the shots deterministic (and to let the physics settle) */
        float dt = shot_frame ? (1.0f / 60.0f) : (float)(now - g.last_time);
        g.last_time = now;
        if (dt > 0.1f) dt = 0.1f;

        g.fps_accum += dt;
        g.fps_frames++;
        if (g.fps_accum >= 0.5f) {
            g.fps_display = (int)(g.fps_frames / g.fps_accum + 0.5);
            g.fps_frames = 0;
            g.fps_accum = 0.0;
            set_title();
        }

        /* the camera */
        sdge_camera3d_set_aspect(&g.cam, (float)g.fbw / (float)g.fbh);
        sdge_camera3d_orbit(&g.cam, g.cam_center, g.cam_dist,
                            g.cam_yaw, g.cam_pitch);
        sdge_camera3d_update(&g.cam);
        sdge_camera2d_set_viewport(&g.cam2d, g.fbw, g.fbh);
        sdge_camera2d_update(&g.cam2d);

        /* the audio: the listener is the camera */
        float cam_pos[3] = { g.cam.pos[0], g.cam.pos[1], g.cam.pos[2] };
        float cam_fwd[3];
        sdge_camera3d_forward(&g.cam, cam_fwd);
        sdge_audio_listener_position(cam_pos[0], cam_pos[1], cam_pos[2]);
        sdge_audio_listener_orientation(cam_fwd[0], cam_fwd[1], cam_fwd[2],
                                        0.0f, 1.0f, 0.0f);
        if (g.beep_src) {
            float a = (float)now * 0.8f;
            sdge_audio_source_set_position(g.beep_src,
                                           cosf(a) * 2.5f, 1.2f, sinf(a) * 2.5f);
        }

        if (g.character) sdge_model_advance(g.character, dt);

        /* the scenes */
        sdge_scene_mgr_update(&g.scenes, dt);

        /* the world */
        if (gl_ok) {
            sdgr_resize(g.fbw, g.fbh);
            float clear[4] = { 0.05f, 0.06f, 0.09f, 1.0f };
            sdgr_begin_frame(clear);
            sdgr_set_camera(g.cam.view, g.cam.proj, cam_pos);
            sdge_ambient_light_apply(&g.ambient);
            sdge_sun_apply(&g.sun);
            sdge_point_light_apply(&g.lamp, 0);
            sdge_scene_mgr_render(&g.scenes);
            sdgr_end_frame();
        }

        /* the gui */
        if (ui_ok && g.gui) {
            sdge_gui_begin(g.gui, g.fbw, g.fbh);
            sdge_gui_draw(g.gui);
            sdge_scene_mgr_render_gui(&g.scenes, g.gui);
            sdge_gui_end(g.gui);
        }

        if (gl_ok && shot_frame && ++frame_no >= shot_frame) {
            int rc = sdgr_screenshot(shot_path);
            fprintf(stderr, "demo: screenshot '%s' %s\n", shot_path,
                    rc == 0 ? "saved" : "FAILED");
            sdge_set_window_should_close(g.win, true);
        }

        sdge_swap_buffers(g.win);
    }

    /* ------------------------------------------------------------- cleanup */
    if (g.gui) sdge_gui_destroy(g.gui);
    sdga_free_font(g.font_ttf);
    sdga_free_font(g.font_otf);
    if (g.music_src) sdge_audio_source_destroy(g.music_src);
    if (g.music)     sdge_audio_free_buffer(g.music);
    if (g.beep_src) sdge_audio_source_destroy(g.beep_src);
    if (g.beep)     sdge_audio_free_buffer(g.beep);
    sdge_audio_terminate();
    if (g.floor)     sdge_model_destroy(g.floor);
    if (g.character) sdge_model_destroy(g.character);
    sdge_mesh_destroy(&g.box_mesh);
    sdge_mesh_destroy(&g.sphere_mesh);
    sdph3_world_destroy(g.phys3);
    sdph2_world_destroy(g.phys2);
    sdui_shutdown();
    sdgr_shutdown();
    sdge_destroy_window(g.win);
    sdge_terminate();
    return 0;
}
