/* window.h - Silver Diamond Game Engine :: Platform Window Layer
 * GLFW-style API, header-only, Windows + Linux (X11).
 *
 * Usage: in exactly ONE .c file:
 *     #define SDGE_WINDOW_IMPLEMENTATION
 *     #include "window.h"
 *
 * Notes:
 *   - Linux backend uses X11 (link with -lX11). Wayland not implemented.
 *   - OpenGL context creation uses WGL (Win32) / GLX (X11).
 *     Define SDGE_NO_GL if you don't want GL linkage.
 *   - No Vulkan/Metal backend yet - add later via sdge_window_get_native_handle().
 */
#ifndef SDGE_WINDOW_H
#define SDGE_WINDOW_H

#include "../sdge_def.h"

#if defined(_WIN32) || defined(_WIN64)
    #define SDGE_PLATFORM_WINDOWS 1
    #ifndef _CRT_SECURE_NO_WARNINGS
        #define _CRT_SECURE_NO_WARNINGS
    #endif
#elif defined(__linux__)
    #define SDGE_PLATFORM_LINUX 1
#else
    #error "window.h: unsupported platform (need Windows or Linux/X11)"
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- Types ---------------- */
typedef struct sdge_window sdge_window;
typedef struct sdge_monitor sdge_monitor;

typedef enum {
    SDGE_KEY_UNKNOWN = -1,
    SDGE_KEY_SPACE = 32,
    SDGE_KEY_APOSTROPHE = 39,
    SDGE_KEY_COMMA = 44,
    SDGE_KEY_MINUS = 45,
    SDGE_KEY_PERIOD = 46,
    SDGE_KEY_SLASH = 47,
    SDGE_KEY_0 = 48, SDGE_KEY_1, SDGE_KEY_2, SDGE_KEY_3, SDGE_KEY_4,
    SDGE_KEY_5, SDGE_KEY_6, SDGE_KEY_7, SDGE_KEY_8, SDGE_KEY_9,
    SDGE_KEY_SEMICOLON = 59,
    SDGE_KEY_EQUAL = 61,
    SDGE_KEY_A = 65, SDGE_KEY_B, SDGE_KEY_C, SDGE_KEY_D, SDGE_KEY_E,
    SDGE_KEY_F, SDGE_KEY_G, SDGE_KEY_H, SDGE_KEY_I, SDGE_KEY_J,
    SDGE_KEY_K, SDGE_KEY_L, SDGE_KEY_M, SDGE_KEY_N, SDGE_KEY_O,
    SDGE_KEY_P, SDGE_KEY_Q, SDGE_KEY_R, SDGE_KEY_S, SDGE_KEY_T,
    SDGE_KEY_U, SDGE_KEY_V, SDGE_KEY_W, SDGE_KEY_X, SDGE_KEY_Y, SDGE_KEY_Z,
    SDGE_KEY_LEFT_BRACKET = 91,
    SDGE_KEY_BACKSLASH = 92,
    SDGE_KEY_RIGHT_BRACKET = 93,
    SDGE_KEY_GRAVE_ACCENT = 96,
    SDGE_KEY_ESCAPE = 256,
    SDGE_KEY_ENTER = 257,
    SDGE_KEY_TAB = 258,
    SDGE_KEY_BACKSPACE = 259,
    SDGE_KEY_INSERT = 260,
    SDGE_KEY_DELETE = 261,
    SDGE_KEY_RIGHT = 262,
    SDGE_KEY_LEFT = 263,
    SDGE_KEY_DOWN = 264,
    SDGE_KEY_UP = 265,
    SDGE_KEY_PAGE_UP = 266,
    SDGE_KEY_PAGE_DOWN = 267,
    SDGE_KEY_HOME = 268,
    SDGE_KEY_END = 269,
    SDGE_KEY_F1 = 290, SDGE_KEY_F2, SDGE_KEY_F3, SDGE_KEY_F4,
    SDGE_KEY_F5, SDGE_KEY_F6, SDGE_KEY_F7, SDGE_KEY_F8,
    SDGE_KEY_F9, SDGE_KEY_F10, SDGE_KEY_F11, SDGE_KEY_F12,
    SDGE_KEY_LEFT_SHIFT = 340,
    SDGE_KEY_LEFT_CONTROL = 341,
    SDGE_KEY_LEFT_ALT = 342,
    SDGE_KEY_LEFT_SUPER = 343,
    SDGE_KEY_RIGHT_SHIFT = 344,
    SDGE_KEY_RIGHT_CONTROL = 345,
    SDGE_KEY_RIGHT_ALT = 346,
    SDGE_KEY_RIGHT_SUPER = 347,
    SDGE_KEY_LAST = SDGE_KEY_RIGHT_SUPER
} sdge_key;

typedef enum {
    SDGE_MOUSE_BUTTON_LEFT = 0,
    SDGE_MOUSE_BUTTON_RIGHT = 1,
    SDGE_MOUSE_BUTTON_MIDDLE = 2,
    SDGE_MOUSE_BUTTON_LAST = 2
} sdge_mouse_button;

typedef enum {
    SDGE_PRESS = 1,
    SDGE_RELEASE = 0,
    SDGE_REPEAT = 2
} sdge_action;

#define SDGE_KEY_COUNT 512
#define SDGE_MOUSE_BUTTON_COUNT 8

/* ---------------- Init / shutdown ---------------- */
SDGE_API bool sdge_init(void);
SDGE_API void sdge_terminate(void);
SDGE_API const char *sdge_get_version_string(void);

/* ---------------- Window hints ---------------- */
typedef enum {
    SDGE_VISIBLE = 0,
    SDGE_RESIZABLE,
    SDGE_DECORATED,
    SDGE_FOCUSED,
    SDGE_CONTEXT_VERSION_MAJOR,
    SDGE_CONTEXT_VERSION_MINOR,
    SDGE_OPENGL_PROFILE,        /* 0=any, 1=core, 2=compat */
    SDGE_DOUBLEBUFFER,
    SDGE_VSYNC,                 /* 1 on, 0 off */
    SDGE_HINT_LAST
} sdge_hint;

SDGE_API void sdge_default_window_hints(void);
SDGE_API void sdge_window_hint(sdge_hint hint, int value);

/* ---------------- Window ---------------- */
SDGE_API sdge_window *sdge_create_window(int width, int height,
                                          const char *title,
                                          sdge_window *share_with);
SDGE_API void         sdge_destroy_window(sdge_window *w);

SDGE_API void         sdge_show_window(sdge_window *w);
SDGE_API void         sdge_hide_window(sdge_window *w);
SDGE_API void         sdge_set_window_title(sdge_window *w, const char *title);
SDGE_API void         sdge_set_window_size(sdge_window *w, int width, int height);
SDGE_API void         sdge_get_window_size(const sdge_window *w, int *out_w, int *out_h);
SDGE_API void         sdge_get_framebuffer_size(const sdge_window *w, int *out_w, int *out_h);

SDGE_API bool         sdge_window_should_close(const sdge_window *w);
SDGE_API void         sdge_set_window_should_close(sdge_window *w, bool value);

SDGE_API void         sdge_set_fullscreen(sdge_window *w, bool fullscreen);
SDGE_API bool         sdge_is_fullscreen(const sdge_window *w);

SDGE_API void         sdge_make_context_current(sdge_window *w);
SDGE_API void         sdge_swap_buffers(sdge_window *w);
SDGE_API void         sdge_swap_interval(int interval);  /* 0=off, 1=on (global) */

/* Native handles (for Vulkan/D3D/embedding) */
SDGE_API void        *sdge_window_get_native_handle(const sdge_window *w);   /* HWND / Window */
SDGE_API void        *sdge_window_get_native_display(const sdge_window *w);  /* HINSTANCE / Display* */

/* ---------------- Monitor ---------------- */
SDGE_API sdge_monitor *sdge_get_primary_monitor(void);
SDGE_API void          sdge_get_monitor_size(const sdge_monitor *m, int *out_w, int *out_h);

/* ---------------- Events ---------------- */
SDGE_API void sdge_poll_events(void);
SDGE_API void sdge_wait_events(void);
SDGE_API void sdge_wait_events_timeout(double seconds);

/* Event callbacks */
typedef void (*sdge_key_fn)(sdge_window *w, int key, int scancode, int action, int mods);
typedef void (*sdge_char_fn)(sdge_window *w, unsigned codepoint);
typedef void (*sdge_mouse_button_fn)(sdge_window *w, int button, int action, int mods);
typedef void (*sdge_cursor_pos_fn)(sdge_window *w, double x, double y);
typedef void (*sdge_scroll_fn)(sdge_window *w, double dx, double dy);
typedef void (*sdge_window_size_fn)(sdge_window *w, int width, int height);
typedef void (*sdge_window_close_fn)(sdge_window *w);
typedef void (*sdge_focus_fn)(sdge_window *w, int focused);
typedef void (*sdge_error_fn)(int code, const char *description);

SDGE_API void sdge_set_key_callback(sdge_window *w, sdge_key_fn fn);
SDGE_API void sdge_set_char_callback(sdge_window *w, sdge_char_fn fn);
SDGE_API void sdge_set_mouse_button_callback(sdge_window *w, sdge_mouse_button_fn fn);
SDGE_API void sdge_set_cursor_pos_callback(sdge_window *w, sdge_cursor_pos_fn fn);
SDGE_API void sdge_set_scroll_callback(sdge_window *w, sdge_scroll_fn fn);
SDGE_API void sdge_set_window_size_callback(sdge_window *w, sdge_window_size_fn fn);
SDGE_API void sdge_set_window_close_callback(sdge_window *w, sdge_window_close_fn fn);
SDGE_API void sdge_set_focus_callback(sdge_window *w, sdge_focus_fn fn);
SDGE_API sdge_error_fn sdge_set_error_callback(sdge_error_fn fn);

/* ---------------- Input state ---------------- */
SDGE_API int    sdge_get_key(const sdge_window *w, int key);
SDGE_API int    sdge_get_mouse_button(const sdge_window *w, int button);
SDGE_API void   sdge_get_cursor_pos(const sdge_window *w, double *x, double *y);
SDGE_API void   sdge_set_cursor_pos(sdge_window *w, double x, double y);
SDGE_API int    sdge_get_window_attrib(const sdge_window *w, int attrib);

/* Timing */
SDGE_API double sdge_get_time(void);
SDGE_API void   sdge_get_time_ms(int64_t *ms);
SDGE_API uint64_t sdge_get_timer_value(void);
SDGE_API uint64_t sdge_get_timer_frequency(void);

#ifdef __cplusplus
}
#endif

#endif /* SDGE_WINDOW_H */

/* ============================================================
 *                     IMPLEMENTATION
 * ============================================================ */
#ifdef SDGE_WINDOW_IMPLEMENTATION
#ifndef SDGE_WINDOW_IMPLEMENTED
#define SDGE_WINDOW_IMPLEMENTED

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SDGE_PLATFORM_WINDOWS)
    #include <windows.h>
    #include <windowsx.h>
    #include <wchar.h>
    #ifndef SDGE_NO_GL
        #include <GL/gl.h>
    #endif
#else
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
    #include <X11/keysym.h>
    #include <X11/XKBlib.h>
    #include <unistd.h>
    #include <time.h>
    #include <sys/time.h>
    #include <sys/select.h>
    #ifndef SDGE_NO_GL
        #include <GL/glx.h>
    #endif
#endif

#define SDGE_MAX_WINDOWS 16
/* engine-wide version string lives in sdge_def.h (SDGE_VERSION_STRING) */

/* ---------------- Global state ---------------- */
typedef struct {
    int hint[SDGE_HINT_LAST];
} sdge_global;

static sdge_global g_sdge = {0};
static int g_sdge_initialized = 0;
static sdge_error_fn g_error_cb = NULL;

typedef struct sdge_window {
    /* common */
    int width, height;
    int fb_width, fb_height;
    bool should_close;
    bool fullscreen;
    int fs_prev_w, fs_prev_h;   /* windowed client size saved on entering fullscreen */
    bool visible;
    bool focused;
    bool resizable;
    char *title;

    unsigned char keys[SDGE_KEY_COUNT];
    unsigned char mouse_buttons[SDGE_MOUSE_BUTTON_COUNT];
    double cursor_x, cursor_y;
    double scroll_x, scroll_y;

    sdge_key_fn         cb_key;
    sdge_char_fn        cb_char;
    sdge_mouse_button_fn cb_mouse;
    sdge_cursor_pos_fn  cb_cursor;
    sdge_scroll_fn      cb_scroll;
    sdge_window_size_fn cb_size;
    sdge_window_close_fn cb_close;
    sdge_focus_fn       cb_focus;

#if defined(SDGE_PLATFORM_WINDOWS)
    HWND hwnd;
    HDC  hdc;
    HGLRC hglrc;
    WNDCLASSEXW wc;
#else
    Window  xwin;
    Display *dpy;
    Atom    wm_delete;
#ifndef SDGE_NO_GL
    GLXContext glctx;
#endif
    int screen;
#endif
} sdge_window;

static sdge_window *g_windows[SDGE_MAX_WINDOWS] = {0};

/* ---------------- Error reporting ---------------- */
static void sdge__emit_error(int code, const char *msg) {
    if (g_error_cb) g_error_cb(code, msg);
    else fprintf(stderr, "[SDGE] error %d: %s\n", code, msg);
}
sdge_error_fn sdge_set_error_callback(sdge_error_fn fn) {
    sdge_error_fn prev = g_error_cb;
    g_error_cb = fn;
    return prev;
}

/* ---------------- Portable helpers ---------------- */
/* MSVC has _strdup, MinGW.org lacks its declaration, POSIX has strdup -
 * use our own implementation so all toolchains behave identically. */
static char *sdge__strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

/* ---------------- Hints ---------------- */
void sdge_default_window_hints(void) {
    g_sdge.hint[SDGE_VISIBLE]              = 1;
    g_sdge.hint[SDGE_RESIZABLE]            = 1;
    g_sdge.hint[SDGE_DECORATED]            = 1;
    g_sdge.hint[SDGE_FOCUSED]              = 1;
    g_sdge.hint[SDGE_CONTEXT_VERSION_MAJOR]= 3;
    g_sdge.hint[SDGE_CONTEXT_VERSION_MINOR]= 3;
    g_sdge.hint[SDGE_OPENGL_PROFILE]       = 1;  /* core */
    g_sdge.hint[SDGE_DOUBLEBUFFER]         = 1;
    g_sdge.hint[SDGE_VSYNC]                = 1;
}
void sdge_window_hint(sdge_hint hint, int value) {
    if (hint >= 0 && hint < SDGE_HINT_LAST) g_sdge.hint[hint] = value;
}

/* ---------------- Key translation ---------------- */
#if defined(SDGE_PLATFORM_WINDOWS)
static int sdge__win_vk_to_key(WPARAM vk, LPARAM lp) {
    if (vk >= 'A' && vk <= 'Z') return SDGE_KEY_A + (int)(vk - 'A');
    if (vk >= '0' && vk <= '9') return SDGE_KEY_0 + (int)(vk - '0');
    switch (vk) {
        case VK_ESCAPE:    return SDGE_KEY_ESCAPE;
        case VK_RETURN:    return SDGE_KEY_ENTER;
        case VK_TAB:       return SDGE_KEY_TAB;
        case VK_BACK:      return SDGE_KEY_BACKSPACE;
        case VK_INSERT:    return SDGE_KEY_INSERT;
        case VK_DELETE:    return SDGE_KEY_DELETE;
        case VK_RIGHT:     return SDGE_KEY_RIGHT;
        case VK_LEFT:      return SDGE_KEY_LEFT;
        case VK_DOWN:      return SDGE_KEY_DOWN;
        case VK_UP:        return SDGE_KEY_UP;
        case VK_PRIOR:     return SDGE_KEY_PAGE_UP;
        case VK_NEXT:      return SDGE_KEY_PAGE_DOWN;
        case VK_HOME:      return SDGE_KEY_HOME;
        case VK_END:       return SDGE_KEY_END;
        case VK_SPACE:     return SDGE_KEY_SPACE;
        case VK_OEM_1:     return SDGE_KEY_SEMICOLON;
        case VK_OEM_PLUS:  return SDGE_KEY_EQUAL;
        case VK_OEM_COMMA: return SDGE_KEY_COMMA;
        case VK_OEM_MINUS: return SDGE_KEY_MINUS;
        case VK_OEM_PERIOD:return SDGE_KEY_PERIOD;
        case VK_OEM_2:     return SDGE_KEY_SLASH;
        case VK_OEM_3:     return SDGE_KEY_GRAVE_ACCENT;
        case VK_OEM_4:     return SDGE_KEY_LEFT_BRACKET;
        case VK_OEM_5:     return SDGE_KEY_BACKSLASH;
        case VK_OEM_6:     return SDGE_KEY_RIGHT_BRACKET;
        case VK_OEM_7:     return SDGE_KEY_APOSTROPHE;
        case VK_SHIFT:     return (HIWORD(lp) & KF_EXTENDED) ? SDGE_KEY_RIGHT_SHIFT : SDGE_KEY_LEFT_SHIFT;
        case VK_CONTROL:   return (HIWORD(lp) & KF_EXTENDED) ? SDGE_KEY_RIGHT_CONTROL : SDGE_KEY_LEFT_CONTROL;
        case VK_MENU:      return (HIWORD(lp) & KF_EXTENDED) ? SDGE_KEY_RIGHT_ALT : SDGE_KEY_LEFT_ALT;
        case VK_LWIN:      return SDGE_KEY_LEFT_SUPER;
        case VK_RWIN:      return SDGE_KEY_RIGHT_SUPER;
        case VK_F1:        return SDGE_KEY_F1;
        case VK_F2:        return SDGE_KEY_F2;
        case VK_F3:        return SDGE_KEY_F3;
        case VK_F4:        return SDGE_KEY_F4;
        case VK_F5:        return SDGE_KEY_F5;
        case VK_F6:        return SDGE_KEY_F6;
        case VK_F7:        return SDGE_KEY_F7;
        case VK_F8:        return SDGE_KEY_F8;
        case VK_F9:        return SDGE_KEY_F9;
        case VK_F10:       return SDGE_KEY_F10;
        case VK_F11:       return SDGE_KEY_F11;
        case VK_F12:       return SDGE_KEY_F12;
        default:           return SDGE_KEY_UNKNOWN;
    }
}
#endif

#if defined(SDGE_PLATFORM_LINUX)
static int sdge__x11_keycode_to_key(KeySym ks) {
    if (ks >= XK_a && ks <= XK_z) return SDGE_KEY_A + (int)(ks - XK_a);
    if (ks >= XK_A && ks <= XK_Z) return SDGE_KEY_A + (int)(ks - XK_A);
    if (ks >= XK_0 && ks <= XK_9) return SDGE_KEY_0 + (int)(ks - XK_0);
    switch (ks) {
        case XK_Escape:    return SDGE_KEY_ESCAPE;
        case XK_Return:    return SDGE_KEY_ENTER;
        case XK_Tab:       return SDGE_KEY_TAB;
        case XK_BackSpace: return SDGE_KEY_BACKSPACE;
        case XK_Insert:    return SDGE_KEY_INSERT;
        case XK_Delete:    return SDGE_KEY_DELETE;
        case XK_Right:     return SDGE_KEY_RIGHT;
        case XK_Left:      return SDGE_KEY_LEFT;
        case XK_Down:      return SDGE_KEY_DOWN;
        case XK_Up:        return SDGE_KEY_UP;
        case XK_Page_Up:   return SDGE_KEY_PAGE_UP;
        case XK_Page_Down: return SDGE_KEY_PAGE_DOWN;
        case XK_Home:      return SDGE_KEY_HOME;
        case XK_End:       return SDGE_KEY_END;
        case XK_space:     return SDGE_KEY_SPACE;
        case XK_semicolon: return SDGE_KEY_SEMICOLON;
        case XK_equal:     return SDGE_KEY_EQUAL;
        case XK_comma:     return SDGE_KEY_COMMA;
        case XK_minus:     return SDGE_KEY_MINUS;
        case XK_period:    return SDGE_KEY_PERIOD;
        case XK_slash:     return SDGE_KEY_SLASH;
        case XK_grave:     return SDGE_KEY_GRAVE_ACCENT;
        case XK_bracketleft:return SDGE_KEY_LEFT_BRACKET;
        case XK_backslash: return SDGE_KEY_BACKSLASH;
        case XK_bracketright:return SDGE_KEY_RIGHT_BRACKET;
        case XK_apostrophe:return SDGE_KEY_APOSTROPHE;
        case XK_Shift_L:   return SDGE_KEY_LEFT_SHIFT;
        case XK_Shift_R:   return SDGE_KEY_RIGHT_SHIFT;
        case XK_Control_L: return SDGE_KEY_LEFT_CONTROL;
        case XK_Control_R: return SDGE_KEY_RIGHT_CONTROL;
        case XK_Alt_L:     return SDGE_KEY_LEFT_ALT;
        case XK_Alt_R:     return SDGE_KEY_RIGHT_ALT;
        case XK_Super_L:   return SDGE_KEY_LEFT_SUPER;
        case XK_Super_R:   return SDGE_KEY_RIGHT_SUPER;
        case XK_F1:        return SDGE_KEY_F1;
        case XK_F2:        return SDGE_KEY_F2;
        case XK_F3:        return SDGE_KEY_F3;
        case XK_F4:        return SDGE_KEY_F4;
        case XK_F5:        return SDGE_KEY_F5;
        case XK_F6:        return SDGE_KEY_F6;
        case XK_F7:        return SDGE_KEY_F7;
        case XK_F8:        return SDGE_KEY_F8;
        case XK_F9:        return SDGE_KEY_F9;
        case XK_F10:       return SDGE_KEY_F10;
        case XK_F11:       return SDGE_KEY_F11;
        case XK_F12:       return SDGE_KEY_F12;
        default:           return SDGE_KEY_UNKNOWN;
    }
}
#endif

/* ---------------- Version ---------------- */
const char *sdge_get_version_string(void) { return SDGE_VERSION_STRING; }

/* ---------------- Init / terminate ---------------- */
bool sdge_init(void) {
    if (g_sdge_initialized) return true;
    sdge_default_window_hints();

#if defined(SDGE_PLATFORM_WINDOWS)
    /* nothing global needed */
#else
    /* XInitThreads is optional; skip by default */
#endif
    g_sdge_initialized = 1;
    return true;
}

void sdge_terminate(void) {
    if (!g_sdge_initialized) return;
    for (int i = 0; i < SDGE_MAX_WINDOWS; ++i)
        if (g_windows[i]) sdge_destroy_window(g_windows[i]);
    g_sdge_initialized = 0;
}

/* ---------------- Windows backend ---------------- */
#if defined(SDGE_PLATFORM_WINDOWS)

static LRESULT CALLBACK sdge__wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static int sdge__register_class(WNDCLASSEXW *wc) {
    static int registered = 0;
    if (registered) return 1;
    wc->cbSize = sizeof(*wc);
    wc->style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc->lpfnWndProc = sdge__wndproc;
    wc->hInstance = GetModuleHandle(NULL);
    wc->hCursor = LoadCursor(NULL, IDC_ARROW);
    wc->lpszClassName = L"SDGEWindowClass";
    if (!RegisterClassExW(wc)) {
        sdge__emit_error(1, "RegisterClassExW failed");
        return 0;
    }
    registered = 1;
    return 1;
}

#ifndef SDGE_NO_GL
static HGLRC sdge__create_gl_context(HDC hdc, sdge_window *share) {
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        sdge__emit_error(2, "SetPixelFormat failed");
        return NULL;
    }
    HGLRC ctx = wglCreateContext(hdc);
    if (ctx && share && share->hglrc) {
        wglShareLists(share->hglrc, ctx);
    }
    return ctx;
}
#endif

sdge_window *sdge_create_window(int width, int height, const char *title, sdge_window *share_with) {
    int slot = -1;
    if (!g_sdge_initialized) sdge_init();
    (void)share_with;  /* only used in the GL path */
    for (int i = 0; i < SDGE_MAX_WINDOWS; ++i) {
        if (!g_windows[i]) { slot = i; break; }
    }
    if (slot < 0) { sdge__emit_error(5, "too many windows (SDGE_MAX_WINDOWS)"); return NULL; }

    sdge_window *w = (sdge_window *)calloc(1, sizeof(sdge_window));
    if (!w) { sdge__emit_error(3, "OOM"); return NULL; }

    w->width = width; w->height = height;
    w->fb_width = width; w->fb_height = height;
    w->visible = (bool)g_sdge.hint[SDGE_VISIBLE];
    w->title = title ? sdge__strdup(title) : sdge__strdup("Silver Diamond");

    if (!sdge__register_class(&w->wc)) { free(w); return NULL; }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!g_sdge.hint[SDGE_RESIZABLE]) style &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    if (!g_sdge.hint[SDGE_DECORATED]) style = WS_POPUP;
    w->resizable = (bool)g_sdge.hint[SDGE_RESIZABLE];

    /* UTF-8 -> UTF-16 title */
    wchar_t wtitle[512];
    int wtitle_len = MultiByteToWideChar(CP_UTF8, 0,
                                         w->title, -1, wtitle, (int)(sizeof(wtitle) / sizeof(wtitle[0])));
    if (wtitle_len <= 0) { wcscpy(wtitle, L"Silver Diamond"); }

    RECT rc = {0, 0, width, height};
    AdjustWindowRect(&rc, style, FALSE);

    w->hwnd = CreateWindowExW(
        0, L"SDGEWindowClass", wtitle,
        style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, GetModuleHandle(NULL), w);
    if (!w->hwnd) { sdge__emit_error(4, "CreateWindowExW failed"); free(w->title); free(w); return NULL; }

    w->hdc = GetDC(w->hwnd);
#ifndef SDGE_NO_GL
    w->hglrc = sdge__create_gl_context(w->hdc, share_with);
    if (w->hglrc) wglMakeCurrent(w->hdc, w->hglrc);
#endif

    if (w->visible) ShowWindow(w->hwnd, SW_SHOW);

    g_windows[slot] = w;
    return w;
}

void sdge_destroy_window(sdge_window *w) {
    if (!w) return;
#ifndef SDGE_NO_GL
    if (w->hglrc) { wglMakeCurrent(NULL, NULL); wglDeleteContext(w->hglrc); }
#endif
    if (w->hdc) ReleaseDC(w->hwnd, w->hdc);
    if (w->hwnd) DestroyWindow(w->hwnd);
    for (int i = 0; i < SDGE_MAX_WINDOWS; ++i)
        if (g_windows[i] == w) g_windows[i] = NULL;
    free(w->title);
    free(w);
}

static LRESULT CALLBACK sdge__wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    sdge_window *w = (sdge_window *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_CLOSE:
            if (w) {
                w->should_close = true;
                if (w->cb_close) w->cb_close(w);
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (w) {
                w->width  = LOWORD(lp);
                w->height = HIWORD(lp);
                w->fb_width = w->width;
                w->fb_height = w->height;
                if (w->cb_size) w->cb_size(w, w->width, w->height);
            }
            return 0;
        case WM_SETFOCUS:
            if (w) { w->focused = true; if (w->cb_focus) w->cb_focus(w, 1); }
            return 0;
        case WM_KILLFOCUS:
            if (w) {
                w->focused = false;
                /* release all keys/buttons, otherwise they stay "pressed" */
                memset(w->keys, 0, sizeof(w->keys));
                memset(w->mouse_buttons, 0, sizeof(w->mouse_buttons));
                if (w->cb_focus) w->cb_focus(w, 0);
            }
            return 0;
        case WM_KEYDOWN: case WM_SYSKEYDOWN: {
            if (w) {
                int key = sdge__win_vk_to_key(wp, lp);
                if (key != SDGE_KEY_UNKNOWN && key < SDGE_KEY_COUNT) {
                    int repeat = (lp & (1 << 30)) != 0;
                    w->keys[key] = 1;
                    if (w->cb_key) w->cb_key(w, key, (int)((lp >> 16) & 0xFF), repeat ? SDGE_REPEAT : SDGE_PRESS, 0);
                }
            }
            return 0;
        }
        case WM_KEYUP: case WM_SYSKEYUP: {
            if (w) {
                int key = sdge__win_vk_to_key(wp, lp);
                if (key != SDGE_KEY_UNKNOWN && key < SDGE_KEY_COUNT) {
                    w->keys[key] = 0;
                    if (w->cb_key) w->cb_key(w, key, (int)((lp >> 16) & 0xFF), SDGE_RELEASE, 0);
                }
            }
            return 0;
        }
        case WM_CHAR: {
            if (w && w->cb_char) w->cb_char(w, (unsigned)wp);
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_LBUTTONUP:
        case WM_RBUTTONDOWN: case WM_RBUTTONUP:
        case WM_MBUTTONDOWN: case WM_MBUTTONUP: {
            if (w) {
                int btn = SDGE_MOUSE_BUTTON_LEFT;
                if (msg == WM_RBUTTONDOWN || msg == WM_RBUTTONUP) btn = SDGE_MOUSE_BUTTON_RIGHT;
                else if (msg == WM_MBUTTONDOWN || msg == WM_MBUTTONUP) btn = SDGE_MOUSE_BUTTON_MIDDLE;
                int down = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN);
                w->mouse_buttons[btn] = down ? 1 : 0;
                if (w->cb_mouse) w->cb_mouse(w, btn, down ? SDGE_PRESS : SDGE_RELEASE, 0);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (w) {
                double x = (double)GET_X_LPARAM(lp);
                double y = (double)GET_Y_LPARAM(lp);
                w->cursor_x = x; w->cursor_y = y;
                if (w->cb_cursor) w->cb_cursor(w, x, y);
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (w) {
                short delta = GET_WHEEL_DELTA_WPARAM(wp);
                w->scroll_x = 0;
                w->scroll_y = delta / (double)WHEEL_DELTA;
                if (w->cb_scroll) w->cb_scroll(w, 0.0, w->scroll_y);
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void sdge_make_context_current(sdge_window *w) {
#ifndef SDGE_NO_GL
    if (w && w->hglrc) wglMakeCurrent(w->hdc, w->hglrc);
#else
    (void)w;
#endif
}
void sdge_swap_buffers(sdge_window *w) {
    if (w && w->hdc) SwapBuffers(w->hdc);
}
void sdge_swap_interval(int interval) {
#ifndef SDGE_NO_GL
    typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)(int);
    PFNWGLSWAPINTERVALEXTPROC p = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
    if (p) p(interval);
#else
    (void)interval;
#endif
}

void *sdge_window_get_native_handle(const sdge_window *w) { return w ? (void *)w->hwnd : NULL; }
void *sdge_window_get_native_display(const sdge_window *w) { return w ? (void *)GetModuleHandle(NULL) : NULL; }

void sdge_show_window(sdge_window *w)  { if (w) { ShowWindow(w->hwnd, SW_SHOW); w->visible = true; } }
void sdge_hide_window(sdge_window *w)  { if (w) { ShowWindow(w->hwnd, SW_HIDE); w->visible = false; } }
void sdge_set_window_title(sdge_window *w, const char *title) {
    if (!w) return;
    free(w->title);
    w->title = title ? sdge__strdup(title) : sdge__strdup("");
    if (title) {
        wchar_t wbuf[512];
        MultiByteToWideChar(CP_UTF8, 0, title, -1, wbuf, 512);
        SetWindowTextW(w->hwnd, wbuf);
    }
}
void sdge_set_window_size(sdge_window *w, int width, int height) {
    if (!w || width <= 0 || height <= 0) return;
    /* width/height are client-area sizes (same as sdge_get_window_size) */
    RECT rc = {0, 0, width, height};
    DWORD style = (DWORD)GetWindowLongPtrW(w->hwnd, GWL_STYLE);
    AdjustWindowRect(&rc, style, FALSE);
    SetWindowPos(w->hwnd, NULL, 0, 0,
                 rc.right - rc.left, rc.bottom - rc.top,
                 SWP_NOMOVE | SWP_NOZORDER);
}

void sdge_poll_events(void) {
    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            for (int i = 0; i < SDGE_MAX_WINDOWS; ++i)
                if (g_windows[i]) g_windows[i]->should_close = true;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
void sdge_wait_events(void) {
    MSG msg;
    if (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
void sdge_wait_events_timeout(double seconds) {
    MsgWaitForMultipleObjects(0, NULL, FALSE, (DWORD)(seconds * 1000.0), QS_ALLINPUT);
    sdge_poll_events();
}

double sdge_get_time(void) {
    static LARGE_INTEGER freq, start;
    static int init = 0;
    LARGE_INTEGER now;
    if (!init) { QueryPerformanceFrequency(&freq); QueryPerformanceCounter(&start); init = 1; }
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - start.QuadPart) / (double)freq.QuadPart;
}
void sdge_get_time_ms(int64_t *ms) { if (ms) *ms = (int64_t)(sdge_get_time() * 1000.0); }
uint64_t sdge_get_timer_value(void) {
    LARGE_INTEGER v; QueryPerformanceCounter(&v); return (uint64_t)v.QuadPart;
}
uint64_t sdge_get_timer_frequency(void) {
    LARGE_INTEGER f; QueryPerformanceFrequency(&f); return (uint64_t)f.QuadPart;
}

/* ---- Windows: no monitors abstraction for now ---- */
typedef struct sdge_monitor { int w, h; } sdge_monitor;
static sdge_monitor g_primary_monitor = {0};
sdge_monitor *sdge_get_primary_monitor(void) {
    g_primary_monitor.w = GetSystemMetrics(SM_CXSCREEN);
    g_primary_monitor.h = GetSystemMetrics(SM_CYSCREEN);
    return &g_primary_monitor;
}
void sdge_get_monitor_size(const sdge_monitor *m, int *out_w, int *out_h) {
    if (!m) return;
    if (out_w) *out_w = m->w;
    if (out_h) *out_h = m->h;
}

void sdge_set_fullscreen(sdge_window *w, bool fullscreen) {
    if (!w) return;
    if (fullscreen == w->fullscreen) return;
    if (fullscreen) {
        w->fs_prev_w = w->width;
        w->fs_prev_h = w->height;
        MONITORINFO mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfo(MonitorFromWindow(w->hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);
        SetWindowLongPtrW(w->hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowPos(w->hwnd, HWND_TOP,
                     mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrW(w->hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        /* restore the client size we had BEFORE entering fullscreen
         * (w->width/height have meanwhile been overwritten by the
         * fullscreen WM_SIZE to the monitor size) */
        int rw = w->fs_prev_w > 0 ? w->fs_prev_w : w->width;
        int rh = w->fs_prev_h > 0 ? w->fs_prev_h : w->height;
        RECT rc = {0, 0, rw, rh};
        AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(w->hwnd, NULL, 100, 100,
                     rc.right - rc.left, rc.bottom - rc.top,
                     SWP_FRAMECHANGED);
    }
    w->fullscreen = fullscreen;
}
bool sdge_is_fullscreen(const sdge_window *w) { return w ? w->fullscreen : false; }

/* ---- Windows: stub callbacks ---- */
void sdge_set_key_callback(sdge_window *w, sdge_key_fn fn)          { if (w) w->cb_key = fn; }
void sdge_set_char_callback(sdge_window *w, sdge_char_fn fn)        { if (w) w->cb_char = fn; }
void sdge_set_mouse_button_callback(sdge_window *w, sdge_mouse_button_fn fn) { if (w) w->cb_mouse = fn; }
void sdge_set_cursor_pos_callback(sdge_window *w, sdge_cursor_pos_fn fn)     { if (w) w->cb_cursor = fn; }
void sdge_set_scroll_callback(sdge_window *w, sdge_scroll_fn fn)    { if (w) w->cb_scroll = fn; }
void sdge_set_window_size_callback(sdge_window *w, sdge_window_size_fn fn)   { if (w) w->cb_size = fn; }
void sdge_set_window_close_callback(sdge_window *w, sdge_window_close_fn fn) { if (w) w->cb_close = fn; }
void sdge_set_focus_callback(sdge_window *w, sdge_focus_fn fn)      { if (w) w->cb_focus = fn; }

/* ---- Windows: input queries ---- */
int sdge_get_key(const sdge_window *w, int key) {
    if (!w || key < 0 || key >= SDGE_KEY_COUNT) return 0;
    return w->keys[key];
}
int sdge_get_mouse_button(const sdge_window *w, int button) {
    if (!w || button < 0 || button >= SDGE_MOUSE_BUTTON_COUNT) return 0;
    return w->mouse_buttons[button];
}
void sdge_get_cursor_pos(const sdge_window *w, double *x, double *y) {
    if (!w) return;
    POINT p; GetCursorPos(&p);
    ScreenToClient(w->hwnd, &p);
    if (x) *x = (double)p.x;
    if (y) *y = (double)p.y;
}
void sdge_set_cursor_pos(sdge_window *w, double x, double y) {
    if (!w) return;
    POINT p = { (LONG)x, (LONG)y };
    ClientToScreen(w->hwnd, &p);
    SetCursorPos(p.x, p.y);
}
int sdge_get_window_attrib(const sdge_window *w, int attrib) {
    if (!w) return 0;
    switch (attrib) {
        case SDGE_VISIBLE:   return w->visible;
        case SDGE_FOCUSED:   return w->focused;
        case SDGE_RESIZABLE: return w->resizable;
        default:             return 0;
    }
}
void sdge_get_window_size(const sdge_window *w, int *out_w, int *out_h) {
    if (!w) return;
    if (out_w) *out_w = w->width;
    if (out_h) *out_h = w->height;
}
void sdge_get_framebuffer_size(const sdge_window *w, int *out_w, int *out_h) {
    if (!w) return;
    if (out_w) *out_w = w->fb_width;
    if (out_h) *out_h = w->fb_height;
}
bool sdge_window_should_close(const sdge_window *w) { return w ? w->should_close : true; }
void sdge_set_window_should_close(sdge_window *w, bool v) { if (w) w->should_close = v; }

#endif /* SDGE_PLATFORM_WINDOWS */

/* ============================================================
 *                        LINUX / X11
 * ============================================================ */
#if defined(SDGE_PLATFORM_LINUX)

struct sdge_monitor { int w, h; };
static sdge_monitor g_primary_monitor = {0};

static Atom g_wm_delete_atom = 0;

/* disable resizing decorations via Motif WM hints (understood by most WMs) */
static void sdge__x11_set_resizable(Display *dpy, Window xwin, bool resizable) {
    if (resizable) return;  /* default WM behaviour */
    struct {
        unsigned long flags, functions, decorations, input_mode, status;
    } mwm = {0, 0, 0, 0, 0};
    const unsigned long MWM_HINTS_FUNCTIONS   = 1;
    const unsigned long MWM_HINTS_DECORATIONS = 2;
    const unsigned long MWM_FUNC_RESIZE       = 1 << 1;
    const unsigned long MWM_FUNC_MAXIMIZE     = 1 << 4;
    const unsigned long MWM_DECOR_MAXIMIZE    = 1 << 5;
    Atom prop = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    mwm.flags = MWM_HINTS_FUNCTIONS | MWM_HINTS_DECORATIONS;
    mwm.functions   = ~0UL & ~(MWM_FUNC_RESIZE | MWM_FUNC_MAXIMIZE);
    mwm.decorations = ~0UL & ~MWM_DECOR_MAXIMIZE;
    XChangeProperty(dpy, xwin, prop, prop, 32, PropModeReplace,
                    (unsigned char *)&mwm, 5);
}

sdge_window *sdge_create_window(int width, int height, const char *title, sdge_window *share_with) {
    int slot = -1;
    if (!g_sdge_initialized) sdge_init();
    (void)share_with;  /* only used in the GL path */
    for (int i = 0; i < SDGE_MAX_WINDOWS; ++i) {
        if (!g_windows[i]) { slot = i; break; }
    }
    if (slot < 0) { sdge__emit_error(5, "too many windows (SDGE_MAX_WINDOWS)"); return NULL; }

    sdge_window *w = (sdge_window *)calloc(1, sizeof(sdge_window));
    if (!w) { sdge__emit_error(3, "OOM"); return NULL; }

    w->dpy = XOpenDisplay(NULL);
    if (!w->dpy) { sdge__emit_error(10, "XOpenDisplay failed"); free(w); return NULL; }

    w->screen = DefaultScreen(w->dpy);
    w->width = width; w->height = height;
    w->fb_width = width; w->fb_height = height;
    w->title = title ? sdge__strdup(title) : sdge__strdup("Silver Diamond");
    w->visible = (bool)g_sdge.hint[SDGE_VISIBLE];
    w->resizable = (bool)g_sdge.hint[SDGE_RESIZABLE];

    if (!g_wm_delete_atom)
        g_wm_delete_atom = XInternAtom(w->dpy, "WM_DELETE_WINDOW", False);

    /* Basic visual for GLX */
    XVisualInfo *vi = NULL;
#ifndef SDGE_NO_GL
    {
        int attribs[16];
        int ai = 0;
        attribs[ai++] = GLX_RGBA;
        attribs[ai++] = GLX_DOUBLEBUFFER;
        attribs[ai++] = GLX_RED_SIZE; attribs[ai++] = 8;
        attribs[ai++] = GLX_GREEN_SIZE; attribs[ai++] = 8;
        attribs[ai++] = GLX_BLUE_SIZE; attribs[ai++] = 8;
        attribs[ai++] = GLX_DEPTH_SIZE; attribs[ai++] = 24;
        attribs[ai++] = None;
        vi = glXChooseVisual(w->dpy, w->screen, attribs);
    }
#endif

    XSetWindowAttributes swa = {0};
    swa.colormap = vi ? XCreateColormap(w->dpy, RootWindow(w->dpy, w->screen),
                                        vi->visual, AllocNone)
                      : DefaultColormap(w->dpy, w->screen);
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask |
                     FocusChangeMask;
    swa.background_pixmap = None;
    swa.border_pixel = 0;

    Window root = RootWindow(w->dpy, w->screen);
    w->xwin = XCreateWindow(w->dpy, root, 0, 0, width, height, 0,
                            vi ? vi->depth : CopyFromParent,
                            InputOutput,
                            vi ? vi->visual : CopyFromParent,
                            CWColormap | CWEventMask | CWBorderPixel,
                            &swa);
    if (!w->xwin) { sdge__emit_error(11, "XCreateWindow failed"); free(w->title); free(w); return NULL; }

    XSetWMProtocols(w->dpy, w->xwin, &g_wm_delete_atom, 1);
    XStoreName(w->dpy, w->xwin, w->title);
    sdge__x11_set_resizable(w->dpy, w->xwin, w->resizable);
    if (w->visible) XMapWindow(w->dpy, w->xwin);

#ifndef SDGE_NO_GL
    if (vi) {
        w->glctx = glXCreateContext(w->dpy, vi, share_with ? share_with->glctx : NULL, True);
        if (w->glctx) glXMakeCurrent(w->dpy, w->xwin, w->glctx);
        XFree(vi);
    }
#endif

    g_windows[slot] = w;
    return w;
}

void sdge_destroy_window(sdge_window *w) {
    if (!w) return;
#ifndef SDGE_NO_GL
    if (w->glctx) {
        glXMakeCurrent(w->dpy, None, NULL);
        glXDestroyContext(w->dpy, w->glctx);
    }
#endif
    if (w->xwin) XDestroyWindow(w->dpy, w->xwin);
    if (w->dpy) XCloseDisplay(w->dpy);
    for (int i = 0; i < SDGE_MAX_WINDOWS; ++i)
        if (g_windows[i] == w) g_windows[i] = NULL;
    free(w->title);
    free(w);
}

void sdge_make_context_current(sdge_window *w) {
#ifndef SDGE_NO_GL
    if (w && w->glctx) glXMakeCurrent(w->dpy, w->xwin, w->glctx);
#else
    (void)w;
#endif
}
void sdge_swap_buffers(sdge_window *w) {
#ifndef SDGE_NO_GL
    if (w && w->dpy) glXSwapBuffers(w->dpy, w->xwin);
#else
    (void)w;
#endif
}
void sdge_swap_interval(int interval) {
#ifndef SDGE_NO_GL
    /* Not all drivers expose this; skip by default to avoid pulling in glxext.h.
       Add glXSwapIntervalEXT/MESA/SGI here if you need it. */
    (void)interval;
#else
    (void)interval;
#endif
}

void *sdge_window_get_native_handle(const sdge_window *w)  { return w ? (void *)(uintptr_t)w->xwin : NULL; }
void *sdge_window_get_native_display(const sdge_window *w) { return w ? (void *)w->dpy : NULL; }

void sdge_show_window(sdge_window *w)  { if (w) { XMapWindow(w->dpy, w->xwin); w->visible = true; } }
void sdge_hide_window(sdge_window *w)  { if (w) { XUnmapWindow(w->dpy, w->xwin); w->visible = false; } }
void sdge_set_window_title(sdge_window *w, const char *title) {
    if (!w) return;
    free(w->title);
    w->title = title ? sdge__strdup(title) : sdge__strdup("");
    XStoreName(w->dpy, w->xwin, w->title);
}
void sdge_set_window_size(sdge_window *w, int width, int height) {
    if (w) XResizeWindow(w->dpy, w->xwin, width, height);
}

void sdge_poll_events(void) {
    for (int i = 0; i < SDGE_MAX_WINDOWS; ++i) {
        sdge_window *w = g_windows[i];
        if (!w || !w->dpy) continue;
        while (XPending(w->dpy)) {
            XEvent ev;
            XNextEvent(w->dpy, &ev);
            switch (ev.type) {
                case ClientMessage:
                    if ((Atom)ev.xclient.data.l[0] == g_wm_delete_atom) {
                        w->should_close = true;
                        if (w->cb_close) w->cb_close(w);
                    }
                    break;
                case ConfigureNotify: {
                    int nw = ev.xconfigure.width, nh = ev.xconfigure.height;
                    if (nw != w->width || nh != w->height) {
                        w->width = nw; w->height = nh;
                        w->fb_width = nw; w->fb_height = nh;
                        if (w->cb_size) w->cb_size(w, nw, nh);
                    }
                    break;
                }
                case FocusIn:
                    w->focused = true;
                    if (w->cb_focus) w->cb_focus(w, 1);
                    break;
                case FocusOut:
                    w->focused = false;
                    /* release all keys/buttons, otherwise they stay "pressed" */
                    memset(w->keys, 0, sizeof(w->keys));
                    memset(w->mouse_buttons, 0, sizeof(w->mouse_buttons));
                    if (w->cb_focus) w->cb_focus(w, 0);
                    break;
                case KeyPress: {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    int key = sdge__x11_keycode_to_key(ks);
                    if (key >= 0 && key < SDGE_KEY_COUNT) w->keys[key] = 1;
                    if (w->cb_key) w->cb_key(w, key, (int)ev.xkey.keycode, SDGE_PRESS, 0);
                    /* naive char event */
                    if (w->cb_char) {
                        char buf[8];
                        KeySym dummy;
                        int n = XLookupString(&ev.xkey, buf, sizeof(buf), &dummy, NULL);
                        if (n == 1 && (unsigned char)buf[0] >= 32)
                            w->cb_char(w, (unsigned char)buf[0]);
                    }
                    break;
                }
                case KeyRelease: {
                    KeySym ks = XLookupKeysym(&ev.xkey, 0);
                    int key = sdge__x11_keycode_to_key(ks);
                    if (key >= 0 && key < SDGE_KEY_COUNT) w->keys[key] = 0;
                    if (w->cb_key) w->cb_key(w, key, (int)ev.xkey.keycode, SDGE_RELEASE, 0);
                    break;
                }
                case ButtonPress: case ButtonRelease: {
                    int btn = -1;
                    switch (ev.xbutton.button) {
                        case Button1: btn = SDGE_MOUSE_BUTTON_LEFT; break;
                        case Button3: btn = SDGE_MOUSE_BUTTON_RIGHT; break;
                        case Button2: btn = SDGE_MOUSE_BUTTON_MIDDLE; break;
                        case Button4: /* scroll up */
                            if (ev.type == ButtonPress && w->cb_scroll) w->cb_scroll(w, 0, 1.0);
                            break;
                        case Button5: /* scroll down */
                            if (ev.type == ButtonPress && w->cb_scroll) w->cb_scroll(w, 0, -1.0);
                            break;
                    }
                    if (btn >= 0) {
                        int down = (ev.type == ButtonPress);
                        w->mouse_buttons[btn] = down ? 1 : 0;
                        if (w->cb_mouse) w->cb_mouse(w, btn, down ? SDGE_PRESS : SDGE_RELEASE, 0);
                    }
                    break;
                }
                case MotionNotify:
                    w->cursor_x = (double)ev.xmotion.x;
                    w->cursor_y = (double)ev.xmotion.y;
                    if (w->cb_cursor) w->cb_cursor(w, w->cursor_x, w->cursor_y);
                    break;
            }
        }
    }
}
/* select() on the sockets of ALL window displays (windows can live on
 * different Display connections), then drain every queue. */
static int sdge__x11_wait_fds(double seconds, bool infinite) {
    fd_set fds;
    int maxfd = -1;
    FD_ZERO(&fds);
    for (int i = 0; i < SDGE_MAX_WINDOWS; ++i) {
        sdge_window *w = g_windows[i];
        if (!w || !w->dpy) continue;
        int fd = ConnectionNumber(w->dpy);
        FD_SET(fd, &fds);
        if (fd > maxfd) maxfd = fd;
    }
    if (maxfd < 0) return 0;

    struct timeval tv, *tvp = NULL;
    if (!infinite) {
        tv.tv_sec  = (time_t)seconds;
        tv.tv_usec = (suseconds_t)((seconds - (double)tv.tv_sec) * 1e6);
        tvp = &tv;
    }
    select(maxfd + 1, &fds, NULL, NULL, tvp);
    return 1;
}

void sdge_wait_events(void) {
    if (!sdge__x11_wait_fds(0.0, true)) return;
    sdge_poll_events();
}
void sdge_wait_events_timeout(double seconds) {
    if (!sdge__x11_wait_fds(seconds, false)) return;
    sdge_poll_events();
}

double sdge_get_time(void) {
    static struct timespec start;
    static int init = 0;
    if (!init) { clock_gettime(CLOCK_MONOTONIC, &start); init = 1; }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)(now.tv_sec - start.tv_sec) +
           (double)(now.tv_nsec - start.tv_nsec) / 1e9;
}
void sdge_get_time_ms(int64_t *ms) { if (ms) *ms = (int64_t)(sdge_get_time() * 1000.0); }
uint64_t sdge_get_timer_value(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
uint64_t sdge_get_timer_frequency(void) { return 1000000000ULL; }

sdge_monitor *sdge_get_primary_monitor(void) {
    Display *dpy = XOpenDisplay(NULL);
    if (dpy) {
        g_primary_monitor.w = DisplayWidth(dpy, DefaultScreen(dpy));
        g_primary_monitor.h = DisplayHeight(dpy, DefaultScreen(dpy));
        XCloseDisplay(dpy);
    }
    return &g_primary_monitor;
}
void sdge_get_monitor_size(const sdge_monitor *m, int *out_w, int *out_h) {
    if (!m) return;
    if (out_w) *out_w = m->w;
    if (out_h) *out_h = m->h;
}

void sdge_set_fullscreen(sdge_window *w, bool fullscreen) {
    if (!w || fullscreen == w->fullscreen) return;
    Atom wm_state      = XInternAtom(w->dpy, "_NET_WM_STATE", False);
    Atom fullscreen_at = XInternAtom(w->dpy, "_NET_WM_STATE_FULLSCREEN", False);
    XEvent xev = {0};
    xev.type = ClientMessage;
    xev.xclient.window = w->xwin;
    xev.xclient.message_type = wm_state;
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = fullscreen ? 1 : 0;  /* _NET_WM_STATE_ADD/REMOVE */
    xev.xclient.data.l[1] = (long)fullscreen_at;
    xev.xclient.data.l[2] = 0;
    XSendEvent(w->dpy, DefaultRootWindow(w->dpy), False,
               SubstructureNotifyMask | SubstructureRedirectMask, &xev);
    w->fullscreen = fullscreen;
}
bool sdge_is_fullscreen(const sdge_window *w) { return w ? w->fullscreen : false; }

void sdge_set_key_callback(sdge_window *w, sdge_key_fn fn)          { if (w) w->cb_key = fn; }
void sdge_set_char_callback(sdge_window *w, sdge_char_fn fn)        { if (w) w->cb_char = fn; }
void sdge_set_mouse_button_callback(sdge_window *w, sdge_mouse_button_fn fn) { if (w) w->cb_mouse = fn; }
void sdge_set_cursor_pos_callback(sdge_window *w, sdge_cursor_pos_fn fn)     { if (w) w->cb_cursor = fn; }
void sdge_set_scroll_callback(sdge_window *w, sdge_scroll_fn fn)    { if (w) w->cb_scroll = fn; }
void sdge_set_window_size_callback(sdge_window *w, sdge_window_size_fn fn)   { if (w) w->cb_size = fn; }
void sdge_set_window_close_callback(sdge_window *w, sdge_window_close_fn fn) { if (w) w->cb_close = fn; }
void sdge_set_focus_callback(sdge_window *w, sdge_focus_fn fn)      { if (w) w->cb_focus = fn; }

int sdge_get_key(const sdge_window *w, int key) {
    if (!w || key < 0 || key >= SDGE_KEY_COUNT) return 0;
    return w->keys[key];
}
int sdge_get_mouse_button(const sdge_window *w, int button) {
    if (!w || button < 0 || button >= SDGE_MOUSE_BUTTON_COUNT) return 0;
    return w->mouse_buttons[button];
}
void sdge_get_cursor_pos(const sdge_window *w, double *x, double *y) {
    if (!w) return;
    Window rr, cr; int rx, ry, wx, wy; unsigned mask;
    if (XQueryPointer(w->dpy, w->xwin, &rr, &cr, &rx, &ry, &wx, &wy, &mask)) {
        if (x) *x = (double)wx;
        if (y) *y = (double)wy;
    }
}
void sdge_set_cursor_pos(sdge_window *w, double x, double y) {
    if (w) XWarpPointer(w->dpy, None, w->xwin, 0, 0, 0, 0, (int)x, (int)y);
}
int sdge_get_window_attrib(const sdge_window *w, int attrib) {
    if (!w) return 0;
    switch (attrib) {
        case SDGE_VISIBLE:   return w->visible;
        case SDGE_FOCUSED:   return w->focused;
        case SDGE_RESIZABLE: return w->resizable;
        default:             return 0;
    }
}
void sdge_get_window_size(const sdge_window *w, int *out_w, int *out_h) {
    if (!w) return;
    if (out_w) *out_w = w->width;
    if (out_h) *out_h = w->height;
}
void sdge_get_framebuffer_size(const sdge_window *w, int *out_w, int *out_h) {
    if (!w) return;
    if (out_w) *out_w = w->fb_width;
    if (out_h) *out_h = w->fb_height;
}
bool sdge_window_should_close(const sdge_window *w) { return w ? w->should_close : true; }
void sdge_set_window_should_close(sdge_window *w, bool v) { if (w) w->should_close = v; }

#endif /* SDGE_PLATFORM_LINUX */

#endif /* SDGE_WINDOW_IMPLEMENTED */
#endif /* SDGE_WINDOW_IMPLEMENTATION */
// im fuck this code :(