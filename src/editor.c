/* src/editor.c - Silver Diamond Game Engine :: the scene editor
 *
 * Opens a glTF/GLB file, shows the hierarchy of its nodes and lets the user
 * move, turn, scale and texture the single nodes.  The result is saved as a
 * new glTF file.
 *
 *   sdge_editor [model.glb|model.gltf] [--out file.gltf]
 *
 * The mouse:
 *   left button in the viewport - pick a node / drag it (the tool decides)
 *   right button                - orbit the camera
 *   wheel                       - zoom
 * The keys:
 *   1 2 3 4   - the tool (select, move, rotate, scale)
 *   X Y Z     - the axis constraint, ESC - free again
 *   F         - frame the whole model
 *   Ctrl+S    - save
 */
#include <sdge.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PANEL_LEFT   280.0f
#define PANEL_RIGHT  290.0f
#define ROW_HEIGHT   19.0f

typedef struct {
    sdge_widget base;
    sdge_editor *ed;
    float scroll;
} tree_widget;

static tree_widget g_tree;

typedef struct {
    sdge_window *win;
    int   width, height;
    sdge_editor *ed;
    sdge_gui *gui;

    /* the panels */
    sdge_gui_div left_panel;
    sdge_gui_div right_panel;

    sdge_gui_label title;
    sdge_gui_label file_label;
    sdge_gui_label tree_title;
    sdge_gui_label status;
    sdge_gui_label sel_title;
    sdge_gui_label sel_name;
    sdge_gui_label pos_label;
    sdge_gui_label rot_label;
    sdge_gui_label scale_label;
    sdge_gui_label tool_title;
    sdge_gui_label tex_title;
    sdge_gui_label tex_info;

    sdge_gui_input open_input;
    sdge_gui_input out_input;
    sdge_gui_input tex_input;

    sdge_gui_button open_button;
    sdge_gui_button save_button;
    sdge_gui_button focus_button;
    sdge_gui_button tool_select;
    sdge_gui_button tool_move;
    sdge_gui_button tool_rotate;
    sdge_gui_button tool_scale;
    sdge_gui_button axis_x;
    sdge_gui_button axis_y;
    sdge_gui_button axis_z;
    sdge_gui_button tex_apply;
    sdge_gui_button tex_clear;

    /* the camera */
    int   orbiting;
    double last_x, last_y;
    float yaw, pitch;

    int   mouse_in_view;
} editor_app;

static editor_app app;

/* ------------------------------------------------------------- the style */

static const float COL_PANEL[4]     = { 0.135f, 0.14f, 0.155f, 1.0f };
static const float COL_PANEL2[4]    = { 0.16f, 0.165f, 0.185f, 1.0f };
static const float COL_BORDER[4]    = { 0.24f, 0.25f, 0.28f, 1.0f };
static const float COL_ACCENT[4]    = { 0.20f, 0.55f, 0.92f, 1.0f };
static const float COL_TEXT[4]      = { 0.90f, 0.91f, 0.93f, 1.0f };
static const float COL_MUTED[4]     = { 0.58f, 0.61f, 0.66f, 1.0f };
static const float COL_SELECT[4]    = { 0.14f, 0.32f, 0.52f, 1.0f };
static const float COL_SUCCESS[4]   = { 0.45f, 0.82f, 0.55f, 1.0f };
static const float COL_ERROR[4]     = { 0.94f, 0.46f, 0.42f, 1.0f };

static void style_button(sdge_gui_button *b, int primary) {
    if (primary) {
        memcpy(b->color, COL_ACCENT, sizeof(COL_ACCENT));
        memcpy(b->color_hover, COL_ACCENT, sizeof(COL_ACCENT));
        memcpy(b->color_active, COL_ACCENT, sizeof(COL_ACCENT));
        memcpy(b->text_color, COL_TEXT, sizeof(COL_TEXT));
    } else {
        memcpy(b->color, COL_PANEL2, sizeof(COL_PANEL2));
        memcpy(b->color_hover, COL_ACCENT, sizeof(COL_ACCENT));
        memcpy(b->color_active, COL_ACCENT, sizeof(COL_ACCENT));
        memcpy(b->text_color, COL_TEXT, sizeof(COL_TEXT));
    }
    b->radius = 4.0f;
    b->border_width = 0.0f;
    b->text_size = 12.0f;
    b->shadow_color[3] = 0.0f;
}

/* ------------------------------------------------------------- the tree */

static void tree_draw(sdge_gui *gui, sdge_widget *w) {
    tree_widget *tw = (tree_widget *)w;
    sdge_editor *ed = tw->ed;
    if (!ed) return;

    float x = w->x;
    float y = w->y;
    float h = w->h;

    int count = sdge_editor_item_count(ed);
    int selected = sdge_editor_selected(ed);
    int first = (int)(tw->scroll / ROW_HEIGHT);
    if (first < 0) first = 0;
    int rows = (int)(h / ROW_HEIGHT) + 1;

    for (int i = first; i < count && i < first + rows; ++i) {
        float ry = y + (float)(i - first) * ROW_HEIGHT;
        if (i == selected) {
            sdge_gui_fill(gui, x, ry, w->w, ROW_HEIGHT, COL_SELECT);
        }
        float indent = 10.0f + (float)sdge_editor_item_depth(ed, i) * 13.0f;
        const char *name = sdge_editor_item_name(ed, i);
        int has_mesh = sdge_editor_item_has_mesh(ed, i);
        if (has_mesh) {
            sdge_gui_fill(gui, x + indent - 8.0f, ry + 7.0f, 5.0f, 5.0f,
                          COL_ACCENT);
        }
        sdge_gui_text(gui, x + indent, ry + 3.0f, name, 12.0f, COL_TEXT);
    }
    if (count == 0) {
        sdge_gui_text(gui, x + 10.0f, y + 4.0f, "no model is open", 12.0f,
                      COL_MUTED);
    }
}

static void tree_event(sdge_gui *gui, sdge_widget *w, int type, int param) {
    tree_widget *tw = (tree_widget *)w;
    sdge_editor *ed = tw->ed;
    if (!ed) return;

    if (type == SDGE_WIDGET_SCROLL) {
        float dy = (float)param / 100.0f;
        int count = sdge_editor_item_count(ed);
        float max_scroll = (float)count * ROW_HEIGHT - w->h;
        if (max_scroll < 0.0f) max_scroll = 0.0f;
        tw->scroll -= dy * 40.0f;
        if (tw->scroll < 0.0f) tw->scroll = 0.0f;
        if (tw->scroll > max_scroll) tw->scroll = max_scroll;
        return;
    }
    if (type != SDGE_WIDGET_CLICK) return;

    float mx = 0.0f, my = 0.0f;
    sdge_gui_mouse_pos(gui, &mx, &my);
    int first = (int)(tw->scroll / ROW_HEIGHT);
    if (first < 0) first = 0;
    int row = first + (int)((my - w->y) / ROW_HEIGHT);
    int count = sdge_editor_item_count(ed);
    if (row < 0 || row >= count) return;
    sdge_editor_select(ed, row);
}

/* ------------------------------------------------------------ the status */

static void set_status(const char *text, int ok) {
    sdge_gui_label_set_text(&app.status, text);
    memcpy(app.status.color, ok ? COL_SUCCESS : COL_ERROR,
           sizeof(app.status.color));
}

static void refresh_selection(void) {
    sdge_editor *ed = app.ed;
    int item = sdge_editor_selected(ed);
    if (item < 0) {
        sdge_gui_label_set_text(&app.sel_name, "-");
        sdge_gui_label_set_text(&app.pos_label, "position: -");
        sdge_gui_label_set_text(&app.rot_label, "rotation: -");
        sdge_gui_label_set_text(&app.scale_label, "scale: -");
        sdge_gui_label_set_text(&app.tex_info, "no texture");
        return;
    }
    sdge_gui_label_set_text(&app.sel_name, sdge_editor_item_name(ed, item));

    sdg_vec3 t, s;
    sdg_quat r;
    if (sdge_editor_get_transform(ed, t, r, s) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "position: %.2f  %.2f  %.2f",
                 t[0], t[1], t[2]);
        sdge_gui_label_set_text(&app.pos_label, buf);
        /* the rotation in degrees */
        float ang = 0.0f;
        float w = r[3] < 0.0f ? -r[3] : r[3];
        if (w > 1.0f) w = 1.0f;
        float sxyz = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
        ang = 2.0f * (float)atan2(sqrtf(sxyz), w) * 57.29578f;
        snprintf(buf, sizeof(buf), "rotation: %.1f deg", ang);
        sdge_gui_label_set_text(&app.rot_label, buf);
        snprintf(buf, sizeof(buf), "scale: %.2f  %.2f  %.2f",
                 s[0], s[1], s[2]);
        sdge_gui_label_set_text(&app.scale_label, buf);
    }

    int node = sdge_editor_selected_node(ed);
    const char *tex = sdge_editor_texture_path(ed, node, SDGE_EDIT_TEX_BASE);
    char buf[160];
    if (tex && tex[0]) {
        snprintf(buf, sizeof(buf), "base: %s", tex);
    } else {
        snprintf(buf, sizeof(buf), "no texture");
    }
    sdge_gui_label_set_text(&app.tex_info, buf);
}

/* ------------------------------------------------------------- the tools */

static void on_open(void *user) {
    (void)user;
    const char *path = sdge_gui_input_text(&app.open_input);
    if (!path || !path[0]) {
        set_status("Enter the path of a glTF or GLB file", 0);
        return;
    }
    if (sdge_editor_open(app.ed, path) != 0) {
        set_status(sdge_editor_error(app.ed), 0);
        return;
    }
    sdge_gui_label_set_text(&app.file_label, path);
    set_status("the model is open", 1);
    refresh_selection();
}

static void on_save(void *user) {
    (void)user;
    const char *path = sdge_gui_input_text(&app.out_input);
    if (!path || !path[0]) {
        set_status("Enter the path of the new glTF file", 0);
        return;
    }
    if (sdge_editor_export(app.ed, path) != 0) {
        set_status("cannot save the scene", 0);
        return;
    }
    set_status("the scene is saved", 1);
}

static void on_focus(void *user) {
    (void)user;
    sdge_editor_focus(app.ed);
    set_status("the camera is on the model", 1);
}

static void on_tool(void *user) {
    int tool = (int)(intptr_t)user;
    sdge_editor_set_tool(app.ed, tool);
}

static void on_axis(void *user) {
    int axis = (int)(intptr_t)user;
    int cur = sdge_editor_axis(app.ed);
    sdge_editor_set_axis(app.ed, (cur == axis) ? SDGE_EDIT_AXIS_NONE : axis);
}

static void on_tex_apply(void *user) {
    (void)user;
    int item = sdge_editor_selected(app.ed);
    if (item < 0) {
        set_status("select a node first", 0);
        return;
    }
    const char *path = sdge_gui_input_text(&app.tex_input);
    if (!path || !path[0]) {
        set_status("Enter the path of the image", 0);
        return;
    }
    if (sdge_editor_apply_texture(app.ed, item, path,
                                  SDGE_EDIT_TEX_BASE) != 0) {
        set_status(sdge_editor_error(app.ed), 0);
        return;
    }
    set_status("the texture is on the node", 1);
    refresh_selection();
}

static void on_tex_clear(void *user) {
    (void)user;
    int item = sdge_editor_selected(app.ed);
    if (item < 0) return;
    sdge_editor_clear_texture(app.ed, item, SDGE_EDIT_TEX_BASE);
    set_status("the texture is removed", 1);
    refresh_selection();
}

/* ------------------------------------------------------------- the input */

static int in_viewport(float x, float y) {
    return x >= PANEL_LEFT && x <= (float)app.width - PANEL_RIGHT &&
           y >= 0.0f && y <= (float)app.height;
}

static void on_mouse_button(sdge_window *w, int button, int action, int mods) {
    (void)w;
    double x = 0.0, y = 0.0;
    sdge_get_cursor_pos(app.win, &x, &y);
    app.mouse_in_view = in_viewport((float)x, (float)y);

    if (app.mouse_in_view) {
        if (button == SDGE_MOUSE_BUTTON_LEFT) {
            sdge_editor_mouse_button(app.ed, 0, action == SDGE_PRESS ? 1 : 0);
            if (action == SDGE_PRESS) refresh_selection();
        } else if (button == SDGE_MOUSE_BUTTON_RIGHT) {
            app.orbiting = (action == SDGE_PRESS);
            sdge_get_cursor_pos(app.win, &app.last_x, &app.last_y);
        }
    } else {
        sdge_gui_mouse_button(app.gui, button, action);
    }
    (void)mods;
}

static void on_cursor(sdge_window *w, double x, double y) {
    (void)w;
    app.mouse_in_view = in_viewport((float)x, (float)y);
    if (app.mouse_in_view) {
        if (app.orbiting) {
            app.yaw -= (float)(x - app.last_x) * 0.01f;
            app.pitch += (float)(y - app.last_y) * 0.01f;
            if (app.pitch >  1.5f) app.pitch =  1.5f;
            if (app.pitch < -1.5f) app.pitch = -1.5f;
            app.last_x = x;
            app.last_y = y;
        } else {
            sdge_editor_mouse_move(app.ed, (float)x - PANEL_LEFT, (float)y);
        }
    } else {
        sdge_gui_mouse_move(app.gui, (float)x, (float)y);
    }
}

static void on_scroll(sdge_window *w, double dx, double dy) {
    (void)w; (void)dx;
    double x = 0.0, y = 0.0;
    sdge_get_cursor_pos(app.win, &x, &y);
    if (in_viewport((float)x, (float)y)) {
        sdge_editor_scroll(app.ed, (float)dy);
    } else {
        sdge_gui_scroll(app.gui, (float)dy);
    }
}

static void on_key(sdge_window *w, int key, int scancode, int action, int mods) {
    (void)w; (void)scancode; (void)mods;
    if (action != SDGE_PRESS) return;
    if (key == SDGE_KEY_ESCAPE) {
        sdge_set_window_should_close(app.win, 1);
        return;
    }
    if (app.mouse_in_view) {
        if (key == 'f' || key == 'F') {
            sdge_editor_focus(app.ed);
            return;
        }
        sdge_editor_key(app.ed, key);
        return;
    }
    sdge_gui_key(app.gui, key, action);
}

static void on_char(sdge_window *w, unsigned codepoint) {
    (void)w;
    sdge_gui_char(app.gui, codepoint);
}

static void on_size(sdge_window *w, int width, int height) {
    (void)w; (void)width; (void)height;
    sdge_get_framebuffer_size(app.win, &app.width, &app.height);
}

/* ------------------------------------------------------------- the layout */

static void layout(void) {
    float h = (float)app.height;
    float w = (float)app.width;

    sdge_gui_div_set_rect(&app.left_panel, 0, 0, PANEL_LEFT, h);
    sdge_gui_div_set_rect(&app.right_panel, w - PANEL_RIGHT, 0, PANEL_RIGHT, h);

    app.title.base.x = 18;  app.title.base.y = 18;
    app.title.base.w = PANEL_LEFT - 36;
    app.file_label.base.x = 18; app.file_label.base.y = 42;
    app.file_label.base.w = PANEL_LEFT - 36;

    app.open_input.base.x = 18;  app.open_input.base.y = 66;
    app.open_input.base.w = PANEL_LEFT - 100;
    app.open_button.base.x = PANEL_LEFT - 76; app.open_button.base.y = 64;
    app.open_button.base.w = 58; app.open_button.base.h = 30;

    app.tree_title.base.x = 18; app.tree_title.base.y = 104;
    app.tree_title.base.w = PANEL_LEFT - 36;
    g_tree.base.x = 8; g_tree.base.y = 124;
    g_tree.base.w = PANEL_LEFT - 16;
    g_tree.base.h = h - 250.0f;
    if (g_tree.base.h < 100.0f) g_tree.base.h = 100.0f;

    app.out_input.base.x = 18;  app.out_input.base.y = h - 118.0f;
    app.out_input.base.w = PANEL_LEFT - 100;
    app.save_button.base.x = PANEL_LEFT - 76; app.save_button.base.y = h - 120.0f;
    app.save_button.base.w = 58; app.save_button.base.h = 30;
    app.focus_button.base.x = 18; app.focus_button.base.y = h - 78.0f;
    app.focus_button.base.w = 90; app.focus_button.base.h = 28;
    app.status.base.x = 18; app.status.base.y = h - 40.0f;
    app.status.base.w = PANEL_LEFT - 36;

    /* the right panel */
    float rx = w - PANEL_RIGHT + 18.0f;
    float rw = PANEL_RIGHT - 36.0f;
    app.sel_title.base.x = rx; app.sel_title.base.y = 18; app.sel_title.base.w = rw;
    app.sel_name.base.x = rx; app.sel_name.base.y = 40; app.sel_name.base.w = rw;
    app.pos_label.base.x = rx; app.pos_label.base.y = 64; app.pos_label.base.w = rw;
    app.rot_label.base.x = rx; app.rot_label.base.y = 84; app.rot_label.base.w = rw;
    app.scale_label.base.x = rx; app.scale_label.base.y = 104; app.scale_label.base.w = rw;

    app.tool_title.base.x = rx; app.tool_title.base.y = 132; app.tool_title.base.w = rw;
    float bw = (rw - 16.0f) / 4.0f;
    app.tool_select.base.x = rx;                app.tool_select.base.y = 154;
    app.tool_move.base.x = rx + bw + 4.0f;      app.tool_move.base.y = 154;
    app.tool_rotate.base.x = rx + (bw + 4.0f) * 2.0f; app.tool_rotate.base.y = 154;
    app.tool_scale.base.x = rx + (bw + 4.0f) * 3.0f; app.tool_scale.base.y = 154;
    app.tool_select.base.w = app.tool_move.base.w = bw;
    app.tool_rotate.base.w = app.tool_scale.base.w = bw;
    app.tool_select.base.h = app.tool_move.base.h = 28.0f;
    app.tool_rotate.base.h = app.tool_scale.base.h = 28.0f;

    float bw3 = (rw - 8.0f) / 3.0f;
    app.axis_x.base.x = rx;                  app.axis_x.base.y = 190;
    app.axis_y.base.x = rx + bw3 + 4.0f;     app.axis_y.base.y = 190;
    app.axis_z.base.x = rx + (bw3 + 4.0f) * 2.0f; app.axis_z.base.y = 190;
    app.axis_x.base.w = app.axis_y.base.w = app.axis_z.base.w = bw3;
    app.axis_x.base.h = app.axis_y.base.h = app.axis_z.base.h = 26.0f;

    app.tex_title.base.x = rx; app.tex_title.base.y = 232; app.tex_title.base.w = rw;
    app.tex_input.base.x = rx; app.tex_input.base.y = 254; app.tex_input.base.w = rw;
    app.tex_apply.base.x = rx; app.tex_apply.base.y = 292;
    app.tex_apply.base.w = rw * 0.5f - 3.0f; app.tex_apply.base.h = 28.0f;
    app.tex_clear.base.x = rx + rw * 0.5f + 3.0f; app.tex_clear.base.y = 292;
    app.tex_clear.base.w = rw * 0.5f - 3.0f; app.tex_clear.base.h = 28.0f;
    app.tex_info.base.x = rx; app.tex_info.base.y = 326; app.tex_info.base.w = rw;
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    const char *model_path = "Subjects/scene.glb";
    const char *out_path = "out/scene_edited.gltf";
    const char *tex_path = "";
    int selftest = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) {
            out_path = argv[++i];
        } else if (!strcmp(argv[i], "--tex") && i + 1 < argc) {
            tex_path = argv[++i];
        } else if (!strcmp(argv[i], "--selftest")) {
            selftest = 1;
        } else {
            model_path = argv[i];
        }
    }

    if (!sdge_init()) {
        fprintf(stderr, "editor: the engine could not start\n");
        return 1;
    }
    sdge_window_hint(SDGE_VSYNC, 1);
    sdge_window_hint(SDGE_RESIZABLE, 1);
    app.win = sdge_create_window(1440, 860, "Silver Diamond - Scene Editor",
                                  NULL);
    if (!app.win) {
        fprintf(stderr, "editor: no window\n");
        sdge_terminate();
        return 1;
    }
    sdge_get_framebuffer_size(app.win, &app.width, &app.height);
    sdge_show_window(app.win);
    sdge_make_context_current(app.win);

    if (sdgr_init() != 0) {
        fprintf(stderr, "editor: the renderer is not available\n");
        sdge_destroy_window(app.win);
        sdge_terminate();
        return 1;
    }
    if (sdui_init() != 0) {
        fprintf(stderr, "editor: the ui is not available\n");
        sdgr_shutdown();
        sdge_destroy_window(app.win);
        sdge_terminate();
        return 1;
    }

    app.ed = sdge_editor_create();
    app.gui = sdge_gui_create();
    if (!app.ed || !app.gui) {
        fprintf(stderr, "editor: no memory\n");
        return 1;
    }

    /* the widgets */
    sdge_gui_div_init(&app.left_panel, 0, 0, PANEL_LEFT, 860, COL_PANEL);
    app.left_panel.border_color[0] = COL_BORDER[0];
    app.left_panel.border_color[1] = COL_BORDER[1];
    app.left_panel.border_color[2] = COL_BORDER[2];
    app.left_panel.border_width = 1.0f;
    sdge_gui_div_init(&app.right_panel, 0, 0, PANEL_RIGHT, 860, COL_PANEL);
    app.right_panel.border_color[0] = COL_BORDER[0];
    app.right_panel.border_color[1] = COL_BORDER[1];
    app.right_panel.border_color[2] = COL_BORDER[2];
    app.right_panel.border_width = 1.0f;

    sdge_gui_label_init(&app.title, 18, 18, 240, 22, "SCENE EDITOR", 15.0f,
                        COL_TEXT);
    sdge_gui_label_init(&app.file_label, 18, 42, 240, 18, "-", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.tree_title, 18, 104, 240, 18, "HIERARCHY", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.status, 18, 800, 240, 20, "ready", 11.0f,
                        COL_MUTED);

    sdge_gui_label_init(&app.sel_title, 0, 18, 240, 18, "SELECTION", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.sel_name, 0, 40, 240, 20, "-", 13.0f, COL_TEXT);
    sdge_gui_label_init(&app.pos_label, 0, 64, 240, 18, "position: -", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.rot_label, 0, 84, 240, 18, "rotation: -", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.scale_label, 0, 104, 240, 18, "scale: -", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.tool_title, 0, 132, 240, 18, "TOOL", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.tex_title, 0, 232, 240, 18, "TEXTURE", 11.0f,
                        COL_MUTED);
    sdge_gui_label_init(&app.tex_info, 0, 326, 240, 18, "no texture", 11.0f,
                        COL_MUTED);

    sdge_gui_input_init(&app.open_input, 18, 66, 180, 30, "model.glb");
    sdge_gui_input_set_text(&app.open_input, model_path);
    sdge_gui_input_init(&app.out_input, 18, 700, 180, 30, "out.gltf");
    sdge_gui_input_set_text(&app.out_input, out_path);
    sdge_gui_input_init(&app.tex_input, 0, 254, 240, 30, "texture.png");

    sdge_gui_button_init(&app.open_button, 0, 64, 58, 30, "Open", on_open,
                         NULL);
    style_button(&app.open_button, 1);
    sdge_gui_button_init(&app.save_button, 0, 700, 58, 30, "Save", on_save,
                         NULL);
    style_button(&app.save_button, 1);
    sdge_gui_button_init(&app.focus_button, 0, 760, 90, 28, "Frame (F)",
                         on_focus, NULL);
    style_button(&app.focus_button, 0);

    sdge_gui_button_init(&app.tool_select, 0, 154, 50, 28, "Select",
                         on_tool, (void *)(intptr_t)SDGE_EDIT_SELECT);
    sdge_gui_button_init(&app.tool_move, 0, 154, 50, 28, "Move", on_tool,
                         (void *)(intptr_t)SDGE_EDIT_MOVE);
    sdge_gui_button_init(&app.tool_rotate, 0, 154, 50, 28, "Rotate", on_tool,
                         (void *)(intptr_t)SDGE_EDIT_ROTATE);
    sdge_gui_button_init(&app.tool_scale, 0, 154, 50, 28, "Scale", on_tool,
                         (void *)(intptr_t)SDGE_EDIT_SCALE);
    style_button(&app.tool_select, 0);
    style_button(&app.tool_move, 0);
    style_button(&app.tool_rotate, 0);
    style_button(&app.tool_scale, 0);

    sdge_gui_button_init(&app.axis_x, 0, 190, 50, 26, "X", on_axis,
                         (void *)(intptr_t)SDGE_EDIT_AXIS_X);
    sdge_gui_button_init(&app.axis_y, 0, 190, 50, 26, "Y", on_axis,
                         (void *)(intptr_t)SDGE_EDIT_AXIS_Y);
    sdge_gui_button_init(&app.axis_z, 0, 190, 50, 26, "Z", on_axis,
                         (void *)(intptr_t)SDGE_EDIT_AXIS_Z);
    style_button(&app.axis_x, 0);
    style_button(&app.axis_y, 0);
    style_button(&app.axis_z, 0);

    sdge_gui_button_init(&app.tex_apply, 0, 292, 110, 28, "Apply",
                         on_tex_apply, NULL);
    sdge_gui_button_init(&app.tex_clear, 0, 292, 110, 28, "Clear",
                         on_tex_clear, NULL);
    style_button(&app.tex_apply, 1);
    style_button(&app.tex_clear, 0);

    /* the tree */
    memset(&g_tree, 0, sizeof(g_tree));
    g_tree.base.visible = 1;
    g_tree.base.enabled = 1;
    g_tree.base.draw = tree_draw;
    g_tree.base.event = tree_event;
    g_tree.ed = app.ed;

    /* the widgets of the gui */
    sdge_gui_add(app.gui, &app.left_panel.base);
    sdge_gui_add(app.gui, &app.right_panel.base);
    sdge_gui_add(app.gui, &app.title.base);
    sdge_gui_add(app.gui, &app.file_label.base);
    sdge_gui_add(app.gui, &app.open_input.base);
    sdge_gui_add(app.gui, &app.open_button.base);
    sdge_gui_add(app.gui, &app.tree_title.base);
    sdge_gui_add(app.gui, &g_tree.base);
    sdge_gui_add(app.gui, &app.out_input.base);
    sdge_gui_add(app.gui, &app.save_button.base);
    sdge_gui_add(app.gui, &app.focus_button.base);
    sdge_gui_add(app.gui, &app.status.base);
    sdge_gui_add(app.gui, &app.sel_title.base);
    sdge_gui_add(app.gui, &app.sel_name.base);
    sdge_gui_add(app.gui, &app.pos_label.base);
    sdge_gui_add(app.gui, &app.rot_label.base);
    sdge_gui_add(app.gui, &app.scale_label.base);
    sdge_gui_add(app.gui, &app.tool_title.base);
    sdge_gui_add(app.gui, &app.tool_select.base);
    sdge_gui_add(app.gui, &app.tool_move.base);
    sdge_gui_add(app.gui, &app.tool_rotate.base);
    sdge_gui_add(app.gui, &app.tool_scale.base);
    sdge_gui_add(app.gui, &app.axis_x.base);
    sdge_gui_add(app.gui, &app.axis_y.base);
    sdge_gui_add(app.gui, &app.axis_z.base);
    sdge_gui_add(app.gui, &app.tex_title.base);
    sdge_gui_add(app.gui, &app.tex_input.base);
    sdge_gui_add(app.gui, &app.tex_apply.base);
    sdge_gui_add(app.gui, &app.tex_clear.base);
    sdge_gui_add(app.gui, &app.tex_info.base);
    layout();

    sdge_set_key_callback(app.win, on_key);
    sdge_set_char_callback(app.win, on_char);
    sdge_set_mouse_button_callback(app.win, on_mouse_button);
    sdge_set_cursor_pos_callback(app.win, on_cursor);
    sdge_set_scroll_callback(app.win, on_scroll);
    sdge_set_window_size_callback(app.win, on_size);

    /* the light of the viewport */
    sdgr_set_ambient(0.22f, 0.23f, 0.27f);
    {
        float dir[3] = { -0.45f, -1.0f, -0.35f };
        float color[3] = { 1.0f, 0.97f, 0.92f };
        sdgr_set_sun(dir, color, 2.2f);
    }
    {
        float pos[3] = { 3.0f, 3.5f, 3.0f };
        float col[3] = { 1.0f, 0.9f, 0.8f };
        sdgr_set_point_light(0, pos, col, 1.2f, 20.0f);
    }

    /* the model */
    if (sdge_editor_open(app.ed, model_path) != 0) {
        fprintf(stderr, "editor: cannot open '%s': %s\n", model_path,
                sdge_editor_error(app.ed));
        set_status("the model is not open - use Open", 0);
        sdge_gui_label_set_text(&app.file_label, "-");
    } else {
        sdge_gui_label_set_text(&app.file_label, model_path);
        set_status("the model is open", 1);
    }

    app.yaw = 0.7f;
    app.pitch = 0.25f;

    /* the self test: open, move, texture, export and read the result back */
    if (selftest) {
        int fail = 0;
        printf("selftest: the model '%s' has %d nodes\n", model_path,
               sdge_editor_item_count(app.ed));

        /* the camera of the viewport */
        sdge_camera3d *cam = sdge_editor_camera(app.ed);
        sdge_editor_update_camera(app.ed, app.width - (int)PANEL_LEFT -
                                         (int)PANEL_RIGHT, app.height);
        sdge_editor_orbit(app.ed, app.yaw, app.pitch, cam->dist);
        sdge_camera3d_orbit(cam, cam->center, cam->dist, cam->yaw, cam->pitch);
        sdge_camera3d_update(cam);
        sdge_editor_set_viewport(app.ed, (int)PANEL_LEFT, 0,
                                 app.width - (int)PANEL_LEFT -
                                 (int)PANEL_RIGHT, app.height);

        /* the first node with a mesh */
        int item = -1;
        for (int i = 0; i < sdge_editor_item_count(app.ed); ++i) {
            if (sdge_editor_item_has_mesh(app.ed, i)) { item = i; break; }
        }
        if (item < 0) {
            printf("selftest: FAILED (no node with a mesh)\n");
            fail = 1;
        } else {
            sdge_editor_select(app.ed, item);
            printf("selftest: the node '%s' is selected\n",
                   sdge_editor_item_name(app.ed, item));

            /* the picking: the origin of the node has to be found again */
            {
                int node = sdge_editor_selected_node(app.ed);
                sdg_mat4 world;
                sdge_model_node_world(sdge_editor_model(app.ed), node, world);
                float p[3] = { world[12], world[13], world[14] };
                sdg_vec4 clip;
                sdg_vec4 v = { p[0], p[1], p[2], 1.0f };
                m4_mul_vec4(clip, cam->vp, v);
                if (fabsf(clip[3]) > 1e-6f) {
                    float ndc_x = clip[0] / clip[3];
                    float ndc_y = clip[1] / clip[3];
                    float vw = (float)(app.width - (int)PANEL_LEFT -
                                       (int)PANEL_RIGHT);
                    float sx = (ndc_x * 0.5f + 0.5f) * vw;
                    float sy = (0.5f - ndc_y * 0.5f) * (float)app.height;
                    int hit = sdge_editor_pick(app.ed, sx, sy);
                    printf("selftest: the pick at %.0f %.0f gives the item %d "
                           "(the node is %d)\n", sx, sy, hit, item);
                    if (hit < 0) {
                        printf("selftest: FAILED (the pick found nothing)\n");
                        fail = 1;
                    }
                }
            }

            /* the drag: the node has to follow the mouse */
            {
                sdge_editor_set_tool(app.ed, SDGE_EDIT_MOVE);
                sdg_vec3 t0, s0;
                sdg_quat r0;
                sdge_editor_get_transform(app.ed, t0, r0, s0);

                int node = sdge_editor_selected_node(app.ed);
                sdg_mat4 world;
                sdge_model_node_world(sdge_editor_model(app.ed), node, world);
                float p[3] = { world[12], world[13], world[14] };
                sdg_vec4 clip;
                sdg_vec4 v = { p[0], p[1], p[2], 1.0f };
                m4_mul_vec4(clip, cam->vp, v);
                if (fabsf(clip[3]) > 1e-6f) {
                    float vw = (float)(app.width - (int)PANEL_LEFT -
                                       (int)PANEL_RIGHT);
                    float sx = (clip[0] / clip[3] * 0.5f + 0.5f) * vw;
                    float sy = (0.5f - clip[1] / clip[3] * 0.5f) *
                               (float)app.height;
                    /* press, move 60 pixels to the right, release */
                    sdge_editor_mouse_move(app.ed, sx, sy);
                    sdge_editor_mouse_button(app.ed, 0, 1);
                    sdge_editor_mouse_move(app.ed, sx + 60.0f, sy);
                    sdge_editor_mouse_button(app.ed, 0, 0);

                    sdg_vec3 t1, s1;
                    sdg_quat r1;
                    sdge_editor_get_transform(app.ed, t1, r1, s1);
                    printf("selftest: the drag moved the node by "
                           "%.3f %.3f %.3f\n",
                           t1[0] - t0[0], t1[1] - t0[1], t1[2] - t0[2]);
                    if (fabsf(t1[0] - t0[0]) < 0.001f &&
                        fabsf(t1[1] - t0[1]) < 0.001f &&
                        fabsf(t1[2] - t0[2]) < 0.001f) {
                        printf("selftest: FAILED (the node did not move)\n");
                        fail = 1;
                    }
                }
                sdge_editor_set_tool(app.ed, SDGE_EDIT_SELECT);
            }

            /* move it */
            sdg_vec3 t, s;
            sdg_quat r;
            sdge_editor_get_transform(app.ed, t, r, s);
            float expect_x = t[0] + 1.25f;
            float expect_y = t[1] + 0.5f;
            t[0] += 1.25f;
            t[1] += 0.5f;
            sdge_editor_set_transform(app.ed, t, r, s);

            /* a texture on it */
            if (tex_path && tex_path[0]) {
                if (sdge_editor_apply_texture(app.ed, item, tex_path,
                                              SDGE_EDIT_TEX_BASE) != 0) {
                    printf("selftest: the texture failed: %s\n",
                           sdge_editor_error(app.ed));
                    fail = 1;
                } else {
                    printf("selftest: the texture is on the node\n");
                }
            }

            /* the export */
            if (sdge_editor_export(app.ed, out_path) != 0) {
                printf("selftest: FAILED (the export)\n");
                fail = 1;
            } else {
                /* read the result back */
                sdge_gltf_model *check = sdge_load_gltf(out_path);
                if (!check) {
                    printf("selftest: FAILED (the file cannot be read)\n");
                    fail = 1;
                } else {
                    printf("selftest: the new file has %d nodes, "
                           "%d meshes, %d materials, %d textures\n",
                           check->node_count, check->mesh_count,
                           check->material_count, check->texture_count);
                    int node = sdge_editor_selected_node(app.ed);
                    if (node >= 0 && node < check->node_count) {
                        printf("selftest: the transform of the node is "
                               "%.3f %.3f %.3f\n",
                               check->nodes[node].translation[0],
                               check->nodes[node].translation[1],
                               check->nodes[node].translation[2]);
                        if (fabsf(check->nodes[node].translation[0] -
                                 expect_x) > 0.001f ||
                            fabsf(check->nodes[node].translation[1] -
                                  expect_y) > 0.001f) {
                            printf("selftest: FAILED (the move is not saved)\n");
                            fail = 1;
                        }
                    }
                    sdge_gltf_destroy(check);
                }
            }
        }
        printf("selftest: %s\n", fail ? "FAILED" : "OK");
        sdge_gui_destroy(app.gui);
        sdge_editor_destroy(app.ed);
        sdui_shutdown();
        sdgr_shutdown();
        sdge_destroy_window(app.win);
        sdge_terminate();
        return fail;
    }

    double last = sdge_get_time();
    while (!sdge_window_should_close(app.win)) {
        sdge_poll_events();
        double now = sdge_get_time();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        /* the camera */
        sdge_camera3d *cam = sdge_editor_camera(app.ed);
        sdge_editor_update_camera(app.ed, app.width - (int)PANEL_LEFT -
                                         (int)PANEL_RIGHT, app.height);
        sdge_editor_orbit(app.ed, app.yaw, app.pitch, cam->dist);
        sdge_camera3d_orbit(cam, cam->center, cam->dist, cam->yaw, cam->pitch);
        sdge_camera3d_update(cam);
        sdgr_set_camera(cam->view, cam->proj, cam->pos);

        /* the viewport of the editor */
        sdge_editor_set_viewport(app.ed, (int)PANEL_LEFT, 0,
                                 app.width - (int)PANEL_LEFT -
                                 (int)PANEL_RIGHT, app.height);

        sdgr_resize(app.width, app.height);
        {
            float clear[4] = { 0.07f, 0.075f, 0.085f, 1.0f };
            sdgr_begin_frame(clear);
        }
        sdge_editor_render(app.ed);
        sdgr_end_frame();

        /* the panels */
        sdge_gui_begin(app.gui, app.width, app.height);
        sdge_gui_draw(app.gui);
        sdge_gui_end(app.gui);

        sdge_swap_buffers(app.win);
    }

    sdge_gui_destroy(app.gui);
    sdge_editor_destroy(app.ed);
    sdui_shutdown();
    sdgr_shutdown();
    sdge_destroy_window(app.win);
    sdge_terminate();
    return 0;
}
