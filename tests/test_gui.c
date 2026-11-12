/* test_gui.c - automated tests for the GUI, the scene manager and the 2D
 * camera.
 *
 * These parts of the engine are logic only (the drawing goes to the UI
 * shader), so the whole suite runs headless - no window, no GL context.
 *
 * Exit code = number of failed checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "subjects/2D/gui/gui.h"
#include "subjects/2D/gui/div.h"
#include "subjects/2D/gui/button.h"
#include "subjects/2D/gui/input.h"
#include "subjects/2D/Camera.h"
#include "scene/scene.h"
#include "graphics/window.h"

static int g_failed = 0, g_total = 0;
#define CHECK(cond, name) do { \
    g_total++; \
    if (cond) printf("  [PASS] %s\n", (name)); \
    else { g_failed++; printf("  [FAIL] %s  (%s:%d)\n", (name), __FILE__, __LINE__); } \
} while (0)

/* ---------------------------------------------------------- scene tests */

static int s_entered, s_exited, s_updated, s_rendered, s_events;
static int s_last_event_type, s_last_event_param;

static void sc_enter(sdge_scene *s) { (void)s; s_entered++; }
static void sc_exit(sdge_scene *s)  { (void)s; s_exited++; }
static void sc_update(sdge_scene *s, float dt) { (void)s; (void)dt; s_updated++; }
static void sc_render(sdge_scene *s) { (void)s; s_rendered++; }
static void sc_event(sdge_scene *s, int type, int param) {
    (void)s;
    s_events++;
    s_last_event_type = type;
    s_last_event_param = param;
}

static void test_scenes(void) {
    printf("\n  -- scene manager --\n");
    sdge_scene_manager mgr;
    sdge_scene a, b;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    a.name = "a"; b.name = "b";
    a.on_enter = sc_enter; a.on_exit = sc_exit; a.on_update = sc_update;
    a.on_render = sc_render; a.on_event = sc_event;
    b.on_enter = sc_enter; b.on_exit = sc_exit; b.on_update = sc_update;
    b.on_render = sc_render; b.on_event = sc_event;

    s_entered = s_exited = s_updated = s_rendered = s_events = 0;

    sdge_scene_mgr_init(&mgr);
    CHECK(mgr.current == -1, "scenes: the manager starts without a scene");
    CHECK(sdge_scene_mgr_add(&mgr, &a) == 0, "scenes: add the first scene");
    CHECK(sdge_scene_mgr_add(&mgr, &b) == 1, "scenes: add the second scene");
    CHECK(sdge_scene_mgr_add(&mgr, NULL) == -1, "scenes: a NULL scene is refused");

    sdge_scene_mgr_switch_now(&mgr, 0);
    CHECK(s_entered == 1, "scenes: on_enter is called on the first switch");
    CHECK(sdge_scene_mgr_current(&mgr) == &a, "scenes: the current scene");
    CHECK(s_entered == 1, "scenes: switching to the same scene does nothing");

    sdge_scene_mgr_update(&mgr, 0.016f);
    CHECK(s_updated == 1, "scenes: on_update is called");
    sdge_scene_mgr_render(&mgr);
    CHECK(s_rendered == 1, "scenes: on_render is called");

    sdge_scene_mgr_event(&mgr, SDGE_SCENE_EVENT_KEY, 42);
    CHECK(s_events == 1 && s_last_event_type == SDGE_SCENE_EVENT_KEY
          && s_last_event_param == 42, "scenes: the events are forwarded");

    /* a fade transition */
    sdge_scene_mgr_switch(&mgr, 1);
    CHECK(sdge_scene_mgr_transitioning(&mgr), "scenes: the switch starts a fade");
    CHECK(s_entered == 1, "scenes: the new scene is not entered yet");
    for (int i = 0; i < 100 && sdge_scene_mgr_transitioning(&mgr); ++i)
        sdge_scene_mgr_update(&mgr, 0.05f);
    CHECK(!sdge_scene_mgr_transitioning(&mgr), "scenes: the transition ends");
    CHECK(s_exited == 1 && s_entered == 2, "scenes: exit + enter on the switch");
    CHECK(sdge_scene_mgr_current(&mgr) == &b, "scenes: the scene changed");
    CHECK(sdge_scene_mgr_fade(&mgr) < 0.001f, "scenes: the fade is back to 0");
}

/* ----------------------------------------------------------- gui tests */

static int g_clicks, g_changes, g_widget_events;
static char g_last_text[128];

static void on_click(void *user) {
    (void)user;
    g_clicks++;
}

static void on_change(void *user, const char *text) {
    (void)user;
    g_changes++;
    snprintf(g_last_text, sizeof(g_last_text), "%s", text ? text : "");
}

static void widget_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    (void)gui; (void)w; (void)type; (void)param;
    g_widget_events++;
}
static void test_gui(void) {
    printf("\n  -- gui manager --\n");
    sdge_gui *gui = sdge_gui_create();
    CHECK(gui != NULL, "gui: created");
    if (!gui) return;

    sdge_gui_div div;
    sdge_gui_button btn;
    sdge_gui_input inp;
    sdge_gui_div_init(&div, 100.0f, 100.0f, 200.0f, 80.0f, NULL);
    sdge_gui_button_init(&btn, 100.0f, 200.0f, 120.0f, 30.0f, "GO", on_click, NULL);
    sdge_gui_input_init(&inp, 100.0f, 300.0f, 200.0f, 24.0f, "type here");
    inp.on_change = on_change;

    CHECK(sdge_gui_add(gui, &div.base) == 0, "gui: add a div");
    CHECK(sdge_gui_add(gui, &btn.base) == 1, "gui: add a button");
    CHECK(sdge_gui_add(gui, &inp.base) == 2, "gui: add an input");
    CHECK(sdge_gui_count(gui) == 3, "gui: the widget count");
    CHECK(sdge_widget_contains(&div.base, 150.0f, 150.0f), "gui: the hit test inside");
    CHECK(!sdge_widget_contains(&div.base, 50.0f, 150.0f), "gui: the hit test outside");
    CHECK(sdge_widget_contains(&div.base, 300.0f, 180.0f), "gui: the hit test on the edge");

    /* hover */
    g_clicks = 0;
    sdge_gui_mouse_move(gui, 150.0f, 210.0f);
    CHECK(sdge_gui_hovered(gui) == 1, "gui: the button is hovered");
    CHECK(btn.hovered == 1, "gui: the button knows it is hovered");
    sdge_gui_mouse_move(gui, 150.0f, 110.0f);
    CHECK(sdge_gui_hovered(gui) == 0, "gui: the div is hovered now");
    CHECK(btn.hovered == 0, "gui: the button knows it is not hovered");

    /* click */
    sdge_gui_mouse_move(gui, 150.0f, 210.0f);
    sdge_gui_mouse_button(gui, 0, SDGE_PRESS);
    sdge_gui_mouse_button(gui, 0, SDGE_RELEASE);
    CHECK(g_clicks == 1, "gui: the click reaches the button");

    /* a press and release on different widgets must not click */
    sdge_gui_mouse_move(gui, 150.0f, 210.0f);
    sdge_gui_mouse_button(gui, 0, SDGE_PRESS);
    sdge_gui_mouse_move(gui, 150.0f, 110.0f);
    sdge_gui_mouse_button(gui, 0, SDGE_RELEASE);
    CHECK(g_clicks == 1, "gui: no click when the release is elsewhere");

    /* the click focuses the widget */
    sdge_gui_mouse_move(gui, 150.0f, 310.0f);
    sdge_gui_mouse_button(gui, 0, SDGE_PRESS);
    sdge_gui_mouse_button(gui, 0, SDGE_RELEASE);
    CHECK(sdge_gui_focused(gui) == 2, "gui: the clicked widget is focused");
    CHECK(inp.focused == 1, "gui: the input knows it is focused");

    /* typing */
    g_changes = 0;
    sdge_gui_char(gui, 'a');
    sdge_gui_char(gui, 'b');
    sdge_gui_char(gui, 'c');
    CHECK(strcmp(sdge_gui_input_text(&inp), "abc") == 0, "gui: the text is typed");
    CHECK(g_changes == 3, "gui: every change is reported");

    /* backspace */
    sdge_gui_key(gui, SDGE_KEY_BACKSPACE, SDGE_PRESS);
    CHECK(strcmp(sdge_gui_input_text(&inp), "ab") == 0, "gui: backspace");
    /* the caret moves and deletes the character on its right */
    sdge_gui_key(gui, SDGE_KEY_LEFT, SDGE_PRESS);
    sdge_gui_key(gui, SDGE_KEY_DELETE, SDGE_PRESS);
    CHECK(strcmp(sdge_gui_input_text(&inp), "a") == 0, "gui: delete");
    sdge_gui_key(gui, SDGE_KEY_HOME, SDGE_PRESS);
    sdge_gui_char(gui, 'z');
    CHECK(strcmp(sdge_gui_input_text(&inp), "za") == 0, "gui: home + insert");
    sdge_gui_key(gui, SDGE_KEY_END, SDGE_PRESS);
    sdge_gui_char(gui, '!');
    CHECK(strcmp(sdge_gui_input_text(&inp), "za!") == 0, "gui: end + append");
    CHECK(strcmp(g_last_text, "za!") == 0, "gui: the change text");

    /* the text of an unfocused widget is not changed */
    sdge_gui_set_focus(gui, 1);
    sdge_gui_char(gui, 'x');
    CHECK(strcmp(sdge_gui_input_text(&inp), "za!") == 0,
          "gui: an unfocused input ignores the keys");

    /* remove / clear */
    sdge_gui_remove(gui, &btn.base);
    CHECK(sdge_gui_count(gui) == 2, "gui: remove a widget");
    sdge_gui_clear(gui);
    CHECK(sdge_gui_count(gui) == 0, "gui: clear the list");

    /* a custom widget with its own event handler */
    {
        sdge_widget custom;
        memset(&custom, 0, sizeof(custom));
        custom.x = 0; custom.y = 0; custom.w = 50; custom.h = 50;
        custom.visible = 1; custom.enabled = 1;
        custom.event = widget_event;
        sdge_gui *g2 = sdge_gui_create();
        g_widget_events = 0;
        sdge_gui_add(g2, &custom);
        sdge_gui_mouse_move(g2, 10.0f, 10.0f);
        sdge_gui_mouse_button(g2, 0, SDGE_PRESS);
        sdge_gui_mouse_button(g2, 0, SDGE_RELEASE);
        CHECK(g_widget_events > 0, "gui: a custom widget gets the events");
        sdge_gui_destroy(g2);
    }

    sdge_gui_destroy(gui);
}

/* -------------------------------------------------------- camera tests */

static void test_camera(void) {
    printf("\n  -- 2d camera --\n");
    sdge_camera2d cam;
    sdge_camera2d_init(&cam, 800, 600);
    CHECK(cam.zoom == 1.0f, "camera: the default zoom");

    float w[2];
    sdge_camera2d_screen_to_world(&cam, 400.0f, 300.0f, w);
    CHECK(fabsf(w[0]) < 0.001f && fabsf(w[1]) < 0.001f,
          "camera: the screen centre is the world origin");

    sdge_camera2d_screen_to_world(&cam, 500.0f, 300.0f, w);
    CHECK(fabsf(w[0] - 100.0f) < 0.001f, "camera: +100 px is +100 units");
    sdge_camera2d_screen_to_world(&cam, 400.0f, 200.0f, w);
    CHECK(fabsf(w[1] - 100.0f) < 0.001f, "camera: the screen y is flipped");

    float s[2];
    sdge_camera2d_world_to_screen(&cam, 123.0f, 45.0f, s);
    sdge_camera2d_screen_to_world(&cam, s[0], s[1], w);
    CHECK(fabsf(w[0] - 123.0f) < 0.001f && fabsf(w[1] - 45.0f) < 0.001f,
          "camera: the world/screen round trip");

    sdge_camera2d_set_zoom(&cam, 2.0f);
    sdge_camera2d_update(&cam);
    sdge_camera2d_screen_to_world(&cam, 500.0f, 300.0f, w);
    CHECK(fabsf(w[0] - 200.0f) < 0.001f, "camera: the zoom halves the view");

    sdge_camera2d_set_pos(&cam, 10.0f, 20.0f);
    sdge_camera2d_update(&cam);
    sdge_camera2d_screen_to_world(&cam, 400.0f, 300.0f, w);
    CHECK(fabsf(w[0] - 10.0f) < 0.001f && fabsf(w[1] - 20.0f) < 0.001f,
          "camera: the position moves the view");

    float b[4];
    sdge_camera2d_bounds(&cam, b);
    CHECK(b[2] > 0.0f && b[3] > 0.0f, "camera: the bounds have a size");
}

/* --------------------------------------------------------- text metrics */

static void test_text(void) {
    printf("\n  -- ui text metrics --\n");
    /* the font is 5x7 with one column of spacing */
    float w1 = sdui_text_width("A", 7.0f);
    float w2 = sdui_text_width("AB", 7.0f);
    CHECK(fabsf(w1 - 6.0f) < 0.001f, "text: one glyph is 6 cells wide");
    CHECK(fabsf(w2 - 12.0f) < 0.001f, "text: the width scales with the length");
    CHECK(fabsf(sdui_text_height(14.0f) - 14.0f) < 0.001f,
          "text: the height is the size");
    CHECK(fabsf(sdui_text_width("AB", 14.0f) - 24.0f) < 0.001f,
          "text: the width scales with the size");
    CHECK(sdui_text_width("", 14.0f) == 0.0f, "text: an empty string");
}

int main(void) {
    test_scenes();
    test_gui();
    test_camera();
    test_text();
    printf("\ntest_gui: %d checks, %d failed\n", g_total, g_failed);
    return g_failed;
}
