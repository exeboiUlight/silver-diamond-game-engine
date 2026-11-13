#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define sdge_mkdir(path) _mkdir(path)
#else
#include <sys/types.h>
#define sdge_mkdir(path) mkdir(path, 0777)
#endif

#include <sdge.h>

#define HUB_PATH_MAX 1024

typedef struct {
    sdge_window *window;
    sdge_gui *gui;
    int width;
    int height;

    sdge_gui_div sidebar;
    sdge_gui_div nav_selected;
    sdge_gui_div nav_indicator;
    sdge_gui_div panel;
    sdge_gui_div template_card;
    sdge_gui_div template_icon;
    sdge_gui_label title;
    sdge_gui_label subtitle;
    sdge_gui_label logo_mark;
    sdge_gui_label brand;
    sdge_gui_label brand_subtitle;
    sdge_gui_label nav_section;
    sdge_gui_label nav_projects;
    sdge_gui_label sidebar_version;
    sdge_gui_label panel_title;
    sdge_gui_label panel_subtitle;
    sdge_gui_label template_category;
    sdge_gui_label template_name;
    sdge_gui_label template_description;
    sdge_gui_label template_detail;
    sdge_gui_label settings_heading;
    sdge_gui_label name_label;
    sdge_gui_label location_label;
    sdge_gui_label project_preview;
    sdge_gui_label status;
    sdge_gui_label footer;
    sdge_gui_input name_input;
    sdge_gui_input location_input;
    sdge_gui_button create_button;
    sdge_gui_button quit_button;
} project_hub;

static project_hub hub;

static const float HUB_COLOR_BACKGROUND[4] = { 0.10f, 0.105f, 0.115f, 1.0f };
static const float COLOR_SIDEBAR[4] = { 0.075f, 0.08f, 0.09f, 1.0f };
static const float COLOR_PANEL[4] = { 0.14f, 0.15f, 0.165f, 1.0f };
static const float COLOR_CARD[4] = { 0.105f, 0.115f, 0.13f, 1.0f };
static const float COLOR_BORDER[4] = { 0.23f, 0.25f, 0.28f, 1.0f };
static const float COLOR_ACCENT[4] = { 0.12f, 0.43f, 0.78f, 1.0f };
static const float COLOR_TEXT[4] = { 0.90f, 0.91f, 0.93f, 1.0f };
static const float COLOR_MUTED[4] = { 0.59f, 0.62f, 0.67f, 1.0f };
static const float COLOR_SUCCESS[4] = { 0.50f, 0.84f, 0.59f, 1.0f };
static const float COLOR_ERROR[4] = { 0.96f, 0.48f, 0.43f, 1.0f };

static int is_separator(char c) {
    return c == '/' || c == '\\';
}

static int is_directory(const char *path) {
    struct stat info;
    if (stat(path, &info) != 0) return 0;
    return (info.st_mode & S_IFDIR) != 0;
}

static int make_directory(const char *path) {
    if (sdge_mkdir(path) == 0) return 1;
    return errno == EEXIST && is_directory(path);
}

static int ensure_directory_tree(const char *path) {
    char copy[HUB_PATH_MAX];
    size_t length = strlen(path);
    size_t start = 0;

    if (length == 0 || length >= sizeof(copy)) return 0;
    memcpy(copy, path, length + 1);

    if (length >= 2 && is_separator(copy[0]) && is_separator(copy[1])) {
        size_t i = 2;
        while (i < length && !is_separator(copy[i])) ++i;
        if (i == length) return is_directory(copy);
        ++i;
        while (i < length && !is_separator(copy[i])) ++i;
        start = i < length ? i + 1 : length;
    }

    for (size_t i = start; i <= length; ++i) {
        if (i != length && !is_separator(copy[i])) continue;
        if (i == 0) continue;
        if (i == 2 && copy[1] == ':') continue;

        char saved = copy[i];
        copy[i] = '\0';
        if (copy[0] != '\0' && !make_directory(copy)) {
            copy[i] = saved;
            return 0;
        }
        copy[i] = saved;
    }

    return is_directory(copy);
}

static int valid_project_name(const char *name) {
    if (!name || !name[0]) return 0;
    if (!((name[0] >= 'A' && name[0] <= 'Z') ||
          (name[0] >= 'a' && name[0] <= 'z') ||
          (name[0] >= '0' && name[0] <= '9')))
        return 0;

    for (const char *p = name; *p; ++p) {
        if (!((*p >= 'A' && *p <= 'Z') ||
              (*p >= 'a' && *p <= 'z') ||
              (*p >= '0' && *p <= '9') ||
              *p == '_' || *p == '-'))
            return 0;
    }
    return 1;
}

static int write_file(const char *path, const char *contents) {
    FILE *file = fopen(path, "wb");
    if (!file) return 0;

    size_t length = strlen(contents);
    int ok = fwrite(contents, 1, length, file) == length;
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static void set_status(const char *message, int success) {
    sdge_gui_label_set_text(&hub.status, message);
    memcpy(hub.status.color, success ? COLOR_SUCCESS : COLOR_ERROR,
           sizeof(hub.status.color));
}

static void update_project_preview(void) {
    char preview[128];
    snprintf(preview, sizeof(preview), "Project folder: %s/%s",
             sdge_gui_input_text(&hub.location_input),
             sdge_gui_input_text(&hub.name_input));
    sdge_gui_label_set_text(&hub.project_preview, preview);
}

static void on_project_settings_changed(void *user, const char *text) {
    (void)user;
    (void)text;
    update_project_preview();
}

static void create_project(void *user) {
    (void)user;
    const char *name = sdge_gui_input_text(&hub.name_input);
    const char *location = sdge_gui_input_text(&hub.location_input);
    char project_path[HUB_PATH_MAX];
    char src_path[HUB_PATH_MAX];
    char assets_path[HUB_PATH_MAX];
    char main_path[HUB_PATH_MAX];
    char readme_path[HUB_PATH_MAX];
    struct stat existing;

    if (!valid_project_name(name)) {
        set_status("Use a name starting with a letter or digit; only A-Z, 0-9, _ and - are allowed.", 0);
        return;
    }
    if (!location || !location[0]) {
        set_status("Enter a folder where the project should be created.", 0);
        return;
    }

    size_t location_length = strlen(location);
    while (location_length > 1 && is_separator(location[location_length - 1]) &&
           !(location_length == 3 && location[1] == ':')) {
        --location_length;
    }
    int separator_needed = location_length > 0 &&
                           !is_separator(location[location_length - 1]);
    int written = snprintf(project_path, sizeof(project_path), "%.*s%s%s",
                           (int)location_length, location,
                           separator_needed ? "/" : "", name);
    if (written < 0 || (size_t)written >= sizeof(project_path)) {
        set_status("The project path is too long.", 0);
        return;
    }

    if (stat(project_path, &existing) == 0) {
        set_status("That project folder already exists. Choose another name or location.", 0);
        return;
    }
    if (errno != ENOENT) {
        set_status("Cannot inspect the project location. Check the path and permissions.", 0);
        return;
    }
    if (!ensure_directory_tree(project_path)) {
        set_status("Could not create the project folder. Check the path and permissions.", 0);
        return;
    }
    int src_length = snprintf(src_path, sizeof(src_path), "%s/src", project_path);
    int assets_length = snprintf(assets_path, sizeof(assets_path), "%s/assets",
                                 project_path);
    int main_length = snprintf(main_path, sizeof(main_path), "%s/src/main.c",
                               project_path);
    int readme_path_length = snprintf(readme_path, sizeof(readme_path),
                                      "%s/README.md", project_path);
    if (src_length < 0 || (size_t)src_length >= sizeof(src_path) ||
        assets_length < 0 || (size_t)assets_length >= sizeof(assets_path) ||
        main_length < 0 || (size_t)main_length >= sizeof(main_path) ||
        readme_path_length < 0 ||
        (size_t)readme_path_length >= sizeof(readme_path)) {
        set_status("The project path is too long.", 0);
        return;
    }
    if (!ensure_directory_tree(src_path) || !ensure_directory_tree(assets_path)) {
        set_status("Project folder created, but its source/assets folders could not be created.", 0);
        return;
    }

    char source[1024];
    int source_length = snprintf(
        source, sizeof(source),
        "#include <sdge.h>\n\n"
        "int main(void) {\n"
        "    if (!sdge_init()) return 1;\n"
        "    sdge_window_hint(SDGE_VSYNC, 1);\n"
        "    sdge_window *window = sdge_create_window(1280, 720, \"%s\", NULL);\n"
        "    if (!window) {\n"
        "        sdge_terminate();\n"
        "        return 1;\n"
        "    }\n"
        "    sdge_show_window(window);\n"
        "    sdge_make_context_current(window);\n"
        "    while (!sdge_window_should_close(window)) {\n"
        "        sdge_poll_events();\n"
        "        sdge_swap_buffers(window);\n"
        "    }\n"
        "    sdge_destroy_window(window);\n"
        "    sdge_terminate();\n"
        "    return 0;\n"
        "}\n", name);
    if (source_length < 0 || (size_t)source_length >= sizeof(source)) {
        set_status("Could not prepare the starter source file.", 0);
        return;
    }

    char readme[768];
    int readme_length = snprintf(
        readme, sizeof(readme),
        "# %s\n\n"
        "A starter project created with Silver Diamond Project Hub.\n\n"
        "## Structure\n\n"
        "- `src/main.c` - application entry point\n"
        "- `assets/` - put project assets here\n\n"
        "## Build\n\n"
        "Use the Silver Diamond SDK from the engine repository: include "
        "`API/include` and link the library in `API/lib/<platform>/<variant>`. "
        "On Windows, "
        "keep `sdge.dll` next to the executable when running it.\n",
        name);
    if (readme_length < 0 || (size_t)readme_length >= sizeof(readme) ||
        !write_file(main_path, source) || !write_file(readme_path, readme)) {
        set_status("Project folders were created, but starter files could not be written.", 0);
        return;
    }

    set_status("Project created. Its source file is in src/main.c.", 1);
}

static void quit_hub(void *user) {
    (void)user;
    sdge_set_window_should_close(hub.window, true);
}

static void layout_widgets(void) {
    float width = (float)hub.width;
    float height = (float)hub.height;
    const float sidebar_width = 226.0f;
    float content_x = sidebar_width + 32.0f;
    float content_width = width - content_x - 32.0f;
    float panel_height = height - 162.0f;
    if (content_width < 650.0f) content_width = 650.0f;
    if (panel_height < 480.0f) panel_height = 480.0f;
    float panel_x = content_x;
    float panel_y = 132.0f;
    float template_x = panel_x + 28.0f;
    float template_y = panel_y + 112.0f;
    float template_width = 226.0f;
    float settings_x = template_x + template_width + 34.0f;
    float settings_width = content_width - (settings_x - panel_x) - 30.0f;
    if (settings_width < 260.0f) settings_width = 260.0f;

    sdge_gui_div_set_rect(&hub.sidebar, 0.0f, 0.0f, sidebar_width, height);
    sdge_gui_div_set_rect(&hub.nav_selected, 12.0f, 136.0f,
                          sidebar_width - 24.0f, 42.0f);
    sdge_gui_div_set_rect(&hub.nav_indicator, 12.0f, 136.0f, 3.0f, 42.0f);
    sdge_gui_div_set_rect(&hub.panel, panel_x, panel_y, content_width,
                          panel_height);
    sdge_gui_div_set_rect(&hub.template_card, template_x, template_y,
                          template_width, 308.0f);
    sdge_gui_div_set_rect(&hub.template_icon, template_x + 18.0f,
                          template_y + 18.0f, 46.0f, 46.0f);

    hub.logo_mark.base.x = 24.0f;
    hub.logo_mark.base.y = 25.0f;
    hub.brand.base.x = 68.0f;
    hub.brand.base.y = 22.0f;
    hub.brand_subtitle.base.x = 68.0f;
    hub.brand_subtitle.base.y = 46.0f;
    hub.nav_section.base.x = 24.0f;
    hub.nav_section.base.y = 106.0f;
    hub.nav_projects.base.x = 38.0f;
    hub.nav_projects.base.y = 144.0f;
    hub.sidebar_version.base.x = 24.0f;
    hub.sidebar_version.base.y = height - 90.0f;
    hub.quit_button.base.x = 18.0f;
    hub.quit_button.base.y = height - 56.0f;
    hub.quit_button.base.w = sidebar_width - 36.0f;

    hub.title.base.x = content_x;
    hub.title.base.y = 28.0f;
    hub.title.base.w = content_width - 210.0f;
    hub.subtitle.base.x = content_x;
    hub.subtitle.base.y = 68.0f;
    hub.subtitle.base.w = content_width - 32.0f;

    hub.panel_title.base.x = panel_x + 28.0f;
    hub.panel_title.base.y = panel_y + 22.0f;
    hub.panel_title.base.w = content_width - 56.0f;
    hub.panel_subtitle.base.x = panel_x + 28.0f;
    hub.panel_subtitle.base.y = panel_y + 57.0f;
    hub.panel_subtitle.base.w = content_width - 56.0f;

    hub.template_category.base.x = template_x + 76.0f;
    hub.template_category.base.y = template_y + 20.0f;
    hub.template_name.base.x = template_x + 18.0f;
    hub.template_name.base.y = template_y + 82.0f;
    hub.template_detail.base.x = template_x + 18.0f;
    hub.template_detail.base.y = template_y + 112.0f;
    hub.template_description.base.x = template_x + 18.0f;
    hub.template_description.base.y = template_y + 162.0f;

    hub.settings_heading.base.x = settings_x;
    hub.settings_heading.base.y = template_y + 2.0f;
    hub.settings_heading.base.w = settings_width;
    hub.name_label.base.x = settings_x;
    hub.name_label.base.y = template_y + 48.0f;
    hub.name_input.base.x = settings_x;
    hub.name_input.base.y = template_y + 74.0f;
    hub.name_input.base.w = settings_width;
    hub.location_label.base.x = settings_x;
    hub.location_label.base.y = template_y + 144.0f;
    hub.location_input.base.x = settings_x;
    hub.location_input.base.y = template_y + 170.0f;
    hub.location_input.base.w = settings_width;
    hub.project_preview.base.x = settings_x;
    hub.project_preview.base.y = template_y + 218.0f;
    hub.project_preview.base.w = settings_width;

    hub.status.base.x = panel_x + 28.0f;
    hub.status.base.y = panel_y + panel_height - 88.0f;
    hub.status.base.w = content_width - 248.0f;
    hub.create_button.base.x = panel_x + content_width - 202.0f;
    hub.create_button.base.y = panel_y + panel_height - 82.0f;
    hub.footer.base.x = panel_x + 28.0f;
    hub.footer.base.y = panel_y + panel_height - 42.0f;
    hub.footer.base.w = content_width - 56.0f;
}

static void on_error(int code, const char *description) {
    fprintf(stderr, "engine error %d: %s\n", code, description);
}

static void on_key(sdge_window *window, int key, int scancode, int action, int mods) {
    (void)window;
    (void)scancode;
    (void)mods;
    if (action != SDGE_PRESS) return;
    if (key == SDGE_KEY_ESCAPE) {
        sdge_set_window_should_close(hub.window, true);
        return;
    }
    sdge_gui_key(hub.gui, key, action);
}

static void on_char(sdge_window *window, unsigned codepoint) {
    (void)window;
    sdge_gui_char(hub.gui, codepoint);
}

static void on_mouse_button(sdge_window *window, int button, int action, int mods) {
    (void)window;
    (void)mods;
    sdge_gui_mouse_button(hub.gui, button, action);
}

static void on_cursor(sdge_window *window, double x, double y) {
    (void)window;
    sdge_gui_mouse_move(hub.gui, (float)x, (float)y);
}

static void on_size(sdge_window *window, int width, int height) {
    (void)window;
    (void)width;
    (void)height;
    sdge_get_framebuffer_size(hub.window, &hub.width, &hub.height);
    layout_widgets();
}

static void add_widget(sdge_widget *widget) {
    if (sdge_gui_add(hub.gui, widget) < 0)
        fprintf(stderr, "project hub: could not register a GUI widget\n");
}

static void style_primary_button(sdge_gui_button *button) {
    const float hover[4] = { 0.17f, 0.52f, 0.88f, 1.0f };
    const float active[4] = { 0.09f, 0.35f, 0.66f, 1.0f };
    memcpy(button->color, COLOR_ACCENT, sizeof(button->color));
    memcpy(button->color_hover, hover, sizeof(button->color_hover));
    memcpy(button->color_active, active, sizeof(button->color_active));
    memcpy(button->border_color, COLOR_ACCENT, sizeof(button->border_color));
    memcpy(button->text_color, COLOR_TEXT, sizeof(button->text_color));
    button->radius = 5.0f;
    button->border_width = 0.0f;
    button->shadow_color[3] = 0.0f;
}

int main(void) {
    if (!sdge_init()) {
        fprintf(stderr, "engine initialization failed\n");
        return 1;
    }
    sdge_set_error_callback(on_error);
    sdge_window_hint(SDGE_VSYNC, 1);
    sdge_window_hint(SDGE_RESIZABLE, 1);

    hub.window = sdge_create_window(1280, 800, "Silver Diamond - Project Hub", NULL);
    if (!hub.window) {
        fprintf(stderr, "window creation failed\n");
        sdge_terminate();
        return 1;
    }

    sdge_show_window(hub.window);
    sdge_make_context_current(hub.window);
    sdge_get_framebuffer_size(hub.window, &hub.width, &hub.height);

    if (sdgr_init() != 0) {
        fprintf(stderr, "renderer initialization failed\n");
        sdge_destroy_window(hub.window);
        sdge_terminate();
        return 1;
    }
    if (sdui_init() != 0) {
        fprintf(stderr, "UI initialization failed\n");
        sdgr_shutdown();
        sdge_destroy_window(hub.window);
        sdge_terminate();
        return 1;
    }

    hub.gui = sdge_gui_create();
    if (!hub.gui) {
        fprintf(stderr, "GUI initialization failed\n");
        sdui_shutdown();
        sdgr_shutdown();
        sdge_destroy_window(hub.window);
        sdge_terminate();
        return 1;
    }

    sdge_gui_div_init(&hub.sidebar, 0.0f, 0.0f, 226.0f, 800.0f,
                      COLOR_SIDEBAR);
    sdge_gui_div_init(&hub.nav_selected, 12.0f, 136.0f, 202.0f, 42.0f,
                      COLOR_PANEL);
    hub.nav_selected.radius = 5.0f;
    sdge_gui_div_init(&hub.nav_indicator, 12.0f, 136.0f, 3.0f, 42.0f,
                      COLOR_ACCENT);
    sdge_gui_div_init(&hub.panel, 258.0f, 132.0f, 990.0f, 638.0f,
                      COLOR_PANEL);
    hub.panel.border_color[0] = COLOR_BORDER[0];
    hub.panel.border_color[1] = COLOR_BORDER[1];
    hub.panel.border_color[2] = COLOR_BORDER[2];
    hub.panel.border_color[3] = COLOR_BORDER[3];
    hub.panel.border_width = 1.0f;
    hub.panel.radius = 7.0f;
    sdge_gui_div_init(&hub.template_card, 286.0f, 244.0f, 226.0f, 308.0f,
                      COLOR_CARD);
    hub.template_card.border_color[0] = COLOR_ACCENT[0];
    hub.template_card.border_color[1] = COLOR_ACCENT[1];
    hub.template_card.border_color[2] = COLOR_ACCENT[2];
    hub.template_card.border_color[3] = COLOR_ACCENT[3];
    hub.template_card.border_width = 1.0f;
    hub.template_card.radius = 6.0f;
    sdge_gui_div_init(&hub.template_icon, 304.0f, 262.0f, 46.0f, 46.0f,
                      COLOR_ACCENT);
    hub.template_icon.radius = 8.0f;

    sdge_gui_label_init(&hub.logo_mark, 24.0f, 25.0f, 32.0f, 32.0f,
                        "SD", 15.0f, COLOR_TEXT);
    hub.logo_mark.align = SDGE_GUI_ALIGN_CENTER;
    sdge_gui_label_init(&hub.brand, 68.0f, 22.0f, 144.0f, 22.0f,
                        "SILVER DIAMOND", 14.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.brand_subtitle, 68.0f, 46.0f, 140.0f, 18.0f,
                        "GAME ENGINE", 10.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.nav_section, 24.0f, 106.0f, 160.0f, 18.0f,
                        "WORKSPACE", 10.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.nav_projects, 38.0f, 144.0f, 160.0f, 26.0f,
                        "Projects", 14.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.sidebar_version, 24.0f, 710.0f, 180.0f, 18.0f,
                        "SILVER DIAMOND  0.3", 10.0f, COLOR_MUTED);

    sdge_gui_label_init(&hub.title, 258.0f, 28.0f, 780.0f, 34.0f,
                        "Projects", 25.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.subtitle, 258.0f, 68.0f, 850.0f, 24.0f,
                        "Create and manage your Silver Diamond projects.",
                        13.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.panel_title, 286.0f, 154.0f, 800.0f, 28.0f,
                        "New project", 18.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.panel_subtitle, 286.0f, 189.0f, 850.0f, 20.0f,
                        "Choose a template and configure your project settings.",
                        12.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.template_category, 362.0f, 264.0f, 140.0f, 24.0f,
                        "3D / C", 11.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.template_name, 304.0f, 326.0f, 190.0f, 28.0f,
                        "Empty Project", 15.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.template_detail, 304.0f, 356.0f, 190.0f, 18.0f,
                        "SILVER DIAMOND CORE", 10.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.template_description, 304.0f, 406.0f, 190.0f, 44.0f,
                        "Window and input loop", 12.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.settings_heading, 546.0f, 246.0f, 500.0f, 20.0f,
                        "PROJECT SETTINGS", 11.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.name_label, 546.0f, 292.0f, 500.0f, 24.0f,
                        "Project name", 12.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.location_label, 546.0f, 388.0f, 500.0f, 24.0f,
                        "Location", 12.0f, COLOR_TEXT);
    sdge_gui_label_init(&hub.project_preview, 546.0f, 464.0f, 500.0f, 20.0f,
                        "Project folder: Projects/MyGame", 11.0f, COLOR_MUTED);
    sdge_gui_label_init(&hub.status, 286.0f, 682.0f, 700.0f, 44.0f,
                        "Select a location and name to begin.", 12.0f,
                        COLOR_MUTED);
    sdge_gui_label_init(&hub.footer, 286.0f, 728.0f, 900.0f, 20.0f,
                        "Starter files: src/main.c, README.md, and assets/",
                        11.0f, COLOR_MUTED);

    sdge_gui_input_init(&hub.name_input, 0.0f, 0.0f, 500.0f, 38.0f,
                        "MyGame");
    sdge_gui_input_set_text(&hub.name_input, "MyGame");
    hub.name_input.on_change = on_project_settings_changed;
    sdge_gui_input_init(&hub.location_input, 0.0f, 0.0f, 500.0f, 38.0f,
                        "Projects");
    sdge_gui_input_set_text(&hub.location_input, "Projects");
    hub.location_input.on_change = on_project_settings_changed;
    sdge_gui_button_init(&hub.create_button, 0.0f, 0.0f, 148.0f, 42.0f,
                         "Create project", create_project, NULL);
    style_primary_button(&hub.create_button);
    sdge_gui_button_init(&hub.quit_button, 0.0f, 0.0f, 120.0f, 42.0f,
                         "Exit Hub", quit_hub, NULL);
    hub.quit_button.text_size = 13.0f;
    hub.quit_button.radius = 5.0f;
    hub.quit_button.shadow_color[3] = 0.0f;
    update_project_preview();

    add_widget(&hub.sidebar.base);
    add_widget(&hub.nav_selected.base);
    add_widget(&hub.nav_indicator.base);
    add_widget(&hub.panel.base);
    add_widget(&hub.template_card.base);
    add_widget(&hub.template_icon.base);
    add_widget(&hub.logo_mark.base);
    add_widget(&hub.brand.base);
    add_widget(&hub.brand_subtitle.base);
    add_widget(&hub.nav_section.base);
    add_widget(&hub.nav_projects.base);
    add_widget(&hub.sidebar_version.base);
    add_widget(&hub.title.base);
    add_widget(&hub.subtitle.base);
    add_widget(&hub.panel_title.base);
    add_widget(&hub.panel_subtitle.base);
    add_widget(&hub.template_category.base);
    add_widget(&hub.template_name.base);
    add_widget(&hub.template_detail.base);
    add_widget(&hub.template_description.base);
    add_widget(&hub.settings_heading.base);
    add_widget(&hub.name_label.base);
    add_widget(&hub.name_input.base);
    add_widget(&hub.location_label.base);
    add_widget(&hub.location_input.base);
    add_widget(&hub.project_preview.base);
    add_widget(&hub.create_button.base);
    add_widget(&hub.quit_button.base);
    add_widget(&hub.status.base);
    add_widget(&hub.footer.base);
    layout_widgets();

    sdge_set_key_callback(hub.window, on_key);
    sdge_set_char_callback(hub.window, on_char);
    sdge_set_mouse_button_callback(hub.window, on_mouse_button);
    sdge_set_cursor_pos_callback(hub.window, on_cursor);
    sdge_set_window_size_callback(hub.window, on_size);

    while (!sdge_window_should_close(hub.window)) {
        sdge_poll_events();
        sdgr_resize(hub.width, hub.height);
        sdgr_begin_frame(HUB_COLOR_BACKGROUND);
        sdgr_end_frame();

        sdge_gui_begin(hub.gui, hub.width, hub.height);
        sdge_gui_draw(hub.gui);
        sdge_gui_end(hub.gui);
        sdge_swap_buffers(hub.window);
    }

    sdge_gui_destroy(hub.gui);
    sdui_shutdown();
    sdgr_shutdown();
    sdge_destroy_window(hub.window);
    sdge_terminate();
    return 0;
}
