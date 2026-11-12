/* tests/test_window.c - automated functional tests for core/graphics/window.h
 *
 * Cross-platform (Windows / Linux-X11), fully automated: user input is
 * synthesized through the native API (PostMessage on Win32, XSendEvent on X11),
 * so the suite runs headless (works fine under Xvfb).
 *
 * Exit code = number of failed checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define SDGE_WINDOW_IMPLEMENTATION
#include "../core/graphics/window.h"

#if defined(SDGE_PLATFORM_LINUX)
    #include <unistd.h>
#endif

/* ---------------- test harness ---------------- */
static int g_failed = 0, g_total = 0;
#define CHECK(cond, name) do { \
    g_total++; \
    if (cond) printf("  [PASS] %s\n", (name)); \
    else { g_failed++; printf("  [FAIL] %s  (%s:%d)\n", (name), __FILE__, __LINE__); } \
} while (0)

static void sleep_ms(int ms) {
#if defined(SDGE_PLATFORM_WINDOWS)
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

/* poll events for a while so asynchronous window ops settle
 * (X11 ConfigureNotify, WM reparenting, ...) */
static void pump_for(int ms) {
    double t0 = sdge_get_time();
    while (sdge_get_time() - t0 < ms / 1000.0) {
        sdge_poll_events();
        sleep_ms(2);
    }
}

/* ---------------- callback recorders ---------------- */
static int k_count, k_key, k_action, k_scancode;
static int c_count; static unsigned c_cp;
static int m_count, m_button, m_action;
static int cur_count; static double cur_x, cur_y;
static int scr_count; static double scr_dx, scr_dy;
static int sz_count, sz_w, sz_h;
static int close_count;
static int foc_count, foc_state;
static int err_count, err_code; static char err_msg[256];

/* Expected key/char for the synthetic-input checks. Defaults match the
 * Windows synthesis ('A' / 'x'); on Linux x11_pick_test_key() overrides
 * them according to the ACTIVE keyboard layout. */
static int     g_expect_key  = SDGE_KEY_A;
static unsigned g_expect_char = 'x';

static void on_key(sdge_window *w, int key, int sc, int action, int mods) {
    (void)w; (void)mods; k_count++; k_key = key; k_scancode = sc; k_action = action;
}
static void on_char(sdge_window *w, unsigned cp) { (void)w; c_count++; c_cp = cp; }
static void on_mouse(sdge_window *w, int b, int a, int mods) {
    (void)w; (void)mods; m_count++; m_button = b; m_action = a;
}
static void on_cursor(sdge_window *w, double x, double y) {
    (void)w; cur_count++; cur_x = x; cur_y = y;
}
static void on_scroll(sdge_window *w, double dx, double dy) {
    (void)w; scr_count++; scr_dx = dx; scr_dy = dy;
}
static void on_size(sdge_window *w, int width, int height) {
    (void)w; sz_count++; sz_w = width; sz_h = height;
}
static void on_close(sdge_window *w) { (void)w; close_count++; }
static void on_focus(sdge_window *w, int focused) { (void)w; foc_count++; foc_state = focused; }
static void on_error(int code, const char *desc) {
    err_count++; err_code = code;
    strncpy(err_msg, desc ? desc : "", sizeof(err_msg) - 1);
    err_msg[sizeof(err_msg) - 1] = 0;
}

/* ---------------- native helpers ---------------- */
static int native_title_equals(sdge_window *w, const char *expected) {
#if defined(SDGE_PLATFORM_WINDOWS)
    wchar_t wbuf[512];
    char utf8[2048];
    HWND hwnd = (HWND)sdge_window_get_native_handle(w);
    int n = GetWindowTextW(hwnd, wbuf, (int)(sizeof(wbuf) / sizeof(wbuf[0])));
    if (n <= 0) return 0;
    int m = WideCharToMultiByte(CP_UTF8, 0, wbuf, n, utf8, (int)sizeof(utf8) - 1, NULL, NULL);
    if (m <= 0) return 0;
    utf8[m] = 0;
    return strcmp(utf8, expected) == 0;
#else
    Display *dpy = (Display *)sdge_window_get_native_display(w);
    Window xwin = (Window)(uintptr_t)sdge_window_get_native_handle(w);
    char *name = NULL;
    if (!XFetchName(dpy, xwin, &name) || !name) return 0;
    int eq = (strcmp(name, expected) == 0);
    XFree(name);
    return eq;
#endif
}

#if defined(SDGE_PLATFORM_LINUX)
/* send a synthetic event to the window created by window.h */
static void x11_send(sdge_window *w, XEvent *ev) {
    Display *dpy = (Display *)sdge_window_get_native_display(w);
    ev->xany.display = dpy;
    ev->xany.window = (Window)(uintptr_t)sdge_window_get_native_handle(w);
    XSendEvent(dpy, ev->xany.window, False, NoEventMask, ev);
    XFlush(dpy);
}

/* The engine reads column-0 keysyms, which follow the ACTIVE keyboard
 * layout (e.g. RU on the user's machine -> no latin letters there).
 * Digits are ASCII in practically every layout, so pick a keycode whose
 * column-0 keysym is a digit (fall back to a latin letter). This keeps
 * the test layout-agnostic. */
static KeyCode g_linux_kc = 0;
static KeySym  g_linux_ks = 0;

static void x11_pick_test_key(sdge_window *w) {
    Display *dpy = (Display *)sdge_window_get_native_display(w);
    for (int kc = 8; kc < 256; ++kc) {
        KeySym ks = XkbKeycodeToKeysym(dpy, (KeyCode)kc, 0, 0);
        if (ks >= XK_0 && ks <= XK_9) { g_linux_kc = (KeyCode)kc; g_linux_ks = ks; break; }
    }
    if (!g_linux_kc) {
        for (int kc = 8; kc < 256; ++kc) {
            KeySym ks = XkbKeycodeToKeysym(dpy, (KeyCode)kc, 0, 0);
            if (ks >= XK_a && ks <= XK_z) { g_linux_kc = (KeyCode)kc; g_linux_ks = ks; break; }
        }
    }
    if (!g_linux_kc) return;   /* give up; checks below will show it */
    if (g_linux_ks >= XK_0 && g_linux_ks <= XK_9) {
        g_expect_key = SDGE_KEY_0 + (int)(g_linux_ks - XK_0);
    } else {
        g_expect_key = SDGE_KEY_A + (int)(g_linux_ks - XK_a);
    }
    g_expect_char = (unsigned)g_linux_ks;
}

static void x11_send_close(sdge_window *w) {
    Display *dpy = (Display *)sdge_window_get_native_display(w);
    Window xwin = (Window)(uintptr_t)sdge_window_get_native_handle(w);
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = ClientMessage;
    ev.xclient.message_type = XInternAtom(dpy, "WM_PROTOCOLS", False);
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = (long)XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSendEvent(dpy, xwin, False, NoEventMask, &ev);
    XFlush(dpy);
}
#endif

#if defined(SDGE_PLATFORM_WINDOWS)
static void win_send_close(sdge_window *w) {
    PostMessageW((HWND)sdge_window_get_native_handle(w), WM_CLOSE, 0, 0);
}
#endif

/* ---------------- input synthesis (per platform) ---------------- */
static void synth_key(sdge_window *w, int press, int repeat) {
#if defined(SDGE_PLATFORM_WINDOWS)
    HWND hwnd = (HWND)sdge_window_get_native_handle(w);
    LPARAM lp = 0;
    if (repeat) lp |= (LPARAM)(1 << 30);       /* previous-state bit */
    PostMessageW(hwnd, press ? WM_KEYDOWN : WM_KEYUP, 'A', lp);
#else
    (void)repeat;
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = press ? KeyPress : KeyRelease;
    ev.xkey.keycode = g_linux_kc;
    x11_send(w, &ev);
#endif
}

static void synth_char(sdge_window *w, unsigned ch) {
#if defined(SDGE_PLATFORM_WINDOWS)
    PostMessageW((HWND)sdge_window_get_native_handle(w), WM_CHAR, (WPARAM)ch, 0);
#else
    (void)ch;
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = KeyPress;
    ev.xkey.keycode = g_linux_kc;   /* char comes via XLookupString */
    x11_send(w, &ev);
#endif
}

static void synth_mouse_button(sdge_window *w, int button, int press) {
#if defined(SDGE_PLATFORM_WINDOWS)
    HWND hwnd = (HWND)sdge_window_get_native_handle(w);
    UINT msg = press ? WM_LBUTTONDOWN : WM_LBUTTONUP;
    if (button == SDGE_MOUSE_BUTTON_RIGHT)  msg = press ? WM_RBUTTONDOWN : WM_RBUTTONUP;
    if (button == SDGE_MOUSE_BUTTON_MIDDLE) msg = press ? WM_MBUTTONDOWN : WM_MBUTTONUP;
    PostMessageW(hwnd, msg, 0, 0);
#else
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = press ? ButtonPress : ButtonRelease;
    ev.xbutton.button = (button == SDGE_MOUSE_BUTTON_LEFT) ? Button1 :
                        (button == SDGE_MOUSE_BUTTON_MIDDLE) ? Button2 : Button3;
    x11_send(w, &ev);
#endif
}

static void synth_mouse_move(sdge_window *w, int x, int y) {
#if defined(SDGE_PLATFORM_WINDOWS)
    PostMessageW((HWND)sdge_window_get_native_handle(w), WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
#else
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = MotionNotify;
    ev.xmotion.x = x; ev.xmotion.y = y;
    x11_send(w, &ev);
#endif
}

static void synth_scroll(sdge_window *w, double dy) {
#if defined(SDGE_PLATFORM_WINDOWS)
    short delta = (short)(dy * WHEEL_DELTA);
    PostMessageW((HWND)sdge_window_get_native_handle(w), WM_MOUSEWHEEL,
                 (WPARAM)MAKELPARAM(0, delta), 0);
#else
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = ButtonPress;
    ev.xbutton.button = (dy > 0) ? Button4 : Button5;
    x11_send(w, &ev);
#endif
}

static void synth_focus(sdge_window *w, int focused) {
#if defined(SDGE_PLATFORM_WINDOWS)
    PostMessageW((HWND)sdge_window_get_native_handle(w),
                 focused ? WM_SETFOCUS : WM_KILLFOCUS, 0, 0);
#else
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = focused ? FocusIn : FocusOut;
    x11_send(w, &ev);
#endif
}

static void synth_resize_event(sdge_window *w, int width, int height) {
#if defined(SDGE_PLATFORM_WINDOWS)
    PostMessageW((HWND)sdge_window_get_native_handle(w), WM_SIZE, 0,
                 MAKELPARAM((WORD)width, (WORD)height));
#else
    XEvent ev; memset(&ev, 0, sizeof(ev));
    ev.type = ConfigureNotify;
    ev.xconfigure.width = width; ev.xconfigure.height = height;
    x11_send(w, &ev);
#endif
}

static void send_close_event(sdge_window *w) {
#if defined(SDGE_PLATFORM_WINDOWS)
    win_send_close(w);
#else
    x11_send_close(w);
#endif
}

/* ---------------- tests ---------------- */
int main(void) {
    printf("== Silver Diamond window.h functional tests ==\n");
    printf("version : %s\n", sdge_get_version_string());
#if defined(SDGE_PLATFORM_WINDOWS)
    printf("platform: Windows (Win32/WGL)\n");
#elif defined(SDGE_PLATFORM_LINUX)
    printf("platform: Linux (X11/GLX)\n");
#endif
    printf("--\n");

    /* ===== init ===== */
    CHECK(sdge_init() == true, "sdge_init() returns true");
    CHECK(sdge_init() == true, "sdge_init() is idempotent");
    sdge_set_error_callback(on_error);

    /* ===== create / basic properties ===== */
    sdge_window *w = sdge_create_window(640, 480, "SDGE Test Window", NULL);
    CHECK(w != NULL, "create_window(640x480) != NULL");
    if (!w) { printf("FATAL: cannot continue without a window\n"); return 100; }

    sdge_set_key_callback(w, on_key);
    sdge_set_char_callback(w, on_char);
    sdge_set_mouse_button_callback(w, on_mouse);
    sdge_set_cursor_pos_callback(w, on_cursor);
    sdge_set_scroll_callback(w, on_scroll);
    sdge_set_window_size_callback(w, on_size);
    sdge_set_window_close_callback(w, on_close);
    sdge_set_focus_callback(w, on_focus);

#if defined(SDGE_PLATFORM_LINUX)
    /* pick a keycode that exists in the ACTIVE layout (layout-agnostic test) */
    x11_pick_test_key(w);
    if (g_linux_kc)
        printf("test key: keycode %d -> keysym 0x%lx ('%c'), expect SDGE key %d, char '%c'\n\n",
               (int)g_linux_kc, (unsigned long)g_linux_ks, (char)g_expect_char,
               g_expect_key, (char)g_expect_char);
    else
        printf("WARNING: no usable keycode found in current layout; keyboard checks will fail\n\n");
#endif

    int ww = 0, wh = 0, fw = 0, fh = 0;
    sdge_get_window_size(w, &ww, &wh);
    CHECK(ww == 640 && wh == 480, "initial window size == 640x480");
    sdge_get_framebuffer_size(w, &fw, &fh);
    CHECK(fw == 640 && fh == 480, "initial framebuffer size == 640x480");

    CHECK(sdge_window_get_native_handle(w) != NULL, "native handle != NULL");
    CHECK(sdge_window_get_native_display(w) != NULL, "native display != NULL");
    CHECK(sdge_window_should_close(w) == false, "new window should_close == false");
    CHECK(sdge_get_window_attrib(w, SDGE_VISIBLE) == 1, "VISIBLE attrib == 1 by default");
    CHECK(sdge_get_window_attrib(w, SDGE_RESIZABLE) == 1, "RESIZABLE attrib == 1 by default");
    CHECK(native_title_equals(w, "SDGE Test Window"), "title passed to create_window is applied");

    sdge_set_window_title(w, "Renamed 123");
    CHECK(native_title_equals(w, "Renamed 123"), "set_window_title() applies");
    sdge_set_window_title(w, "Тест ΣΔ 銀");
    CHECK(native_title_equals(w, "Тест ΣΔ 銀"), "UTF-8 title roundtrip");

    pump_for(50);   /* let the WM settle */

    /* ===== show / hide ===== */
    sdge_hide_window(w);
    CHECK(sdge_get_window_attrib(w, SDGE_VISIBLE) == 0, "hide_window() -> VISIBLE == 0");
    sdge_show_window(w);
    CHECK(sdge_get_window_attrib(w, SDGE_VISIBLE) == 1, "show_window() -> VISIBLE == 1");

    /* Hide the window for ALL remaining event tests: a hidden window cannot
     * receive real user input, so the synthetic events below are the only
     * ones in the queue -> deterministic results even with a human at the
     * machine (they were fighting the test suite before). */
    sdge_hide_window(w);
    pump_for(30);

    /* ===== resize ===== */
    sdge_set_window_size(w, 800, 600);
    pump_for(100);
    sdge_get_window_size(w, &ww, &wh);
    CHECK(ww == 800 && wh == 600, "set_window_size(800x600) -> get_window_size == 800x600");
    sdge_get_framebuffer_size(w, &fw, &fh);
    CHECK(fw == 800 && fh == 600, "framebuffer size follows window size");

    sz_count = 0; sz_w = -1; sz_h = -1;
    synth_resize_event(w, 300, 200);
    pump_for(50);
    CHECK(sz_count == 1, "size callback fired exactly once on resize");
    CHECK(sz_w == 300 && sz_h == 200, "size callback reports 300x200");
    sdge_get_window_size(w, &ww, &wh);
    CHECK(ww == 300 && wh == 200, "get_window_size() == 300x200 after resize event");

    /* ===== keyboard ===== */
    k_count = 0; k_key = -1; k_action = -1; k_scancode = -1;
    synth_key(w, 1, 0);
    pump_for(30);
    CHECK(k_count == 1, "key callback fired on key press");
    CHECK(k_key == g_expect_key, "key callback maps to the expected SDGE key");
    CHECK(k_action == SDGE_PRESS, "key press action == SDGE_PRESS");
    CHECK(sdge_get_key(w, g_expect_key) == 1, "get_key(expected key) == 1 while pressed");
    CHECK(sdge_get_key(w, 9999) == 0, "get_key() out-of-range returns 0");
    CHECK(sdge_get_key(NULL, g_expect_key) == 0, "get_key(NULL) returns 0");

#if defined(SDGE_PLATFORM_WINDOWS)
    k_count = 0; k_action = -1;
    synth_key(w, 1, 1);
    pump_for(30);
    CHECK(k_count == 1 && k_action == SDGE_REPEAT, "key repeat (prev-state bit) -> SDGE_REPEAT");
    synth_key(w, 0, 0);
    pump_for(30);
#endif

    synth_key(w, 0, 0);
    pump_for(30);
    CHECK(k_action == SDGE_RELEASE, "key release action == SDGE_RELEASE");
    CHECK(sdge_get_key(w, g_expect_key) == 0, "get_key(expected key) == 0 after release");

    /* ===== char input ===== */
    c_count = 0; c_cp = 0;
    synth_char(w, g_expect_char);
    pump_for(30);
    CHECK(c_count == 1, "char callback fired");
    CHECK(c_cp == g_expect_char, "char callback reports the expected character");

    /* ===== mouse buttons ===== */
    m_count = 0; m_button = -1; m_action = -1;
    synth_mouse_button(w, SDGE_MOUSE_BUTTON_LEFT, 1);
    pump_for(30);
    CHECK(m_count == 1, "mouse callback fired on button press");
    CHECK(m_button == SDGE_MOUSE_BUTTON_LEFT && m_action == SDGE_PRESS, "mouse press params (LEFT)");
    CHECK(sdge_get_mouse_button(w, SDGE_MOUSE_BUTTON_LEFT) == 1, "get_mouse_button(LEFT) == 1");

    synth_mouse_button(w, SDGE_MOUSE_BUTTON_LEFT, 0);
    pump_for(30);
    CHECK(m_action == SDGE_RELEASE, "mouse release action");
    CHECK(sdge_get_mouse_button(w, SDGE_MOUSE_BUTTON_LEFT) == 0, "get_mouse_button(LEFT) == 0");

    synth_mouse_button(w, SDGE_MOUSE_BUTTON_RIGHT, 1);
    pump_for(30);
    CHECK(m_button == SDGE_MOUSE_BUTTON_RIGHT, "right mouse button maps correctly");
    synth_mouse_button(w, SDGE_MOUSE_BUTTON_RIGHT, 0);
    pump_for(30);
    CHECK(sdge_get_mouse_button(w, SDGE_MOUSE_BUTTON_RIGHT) == 0, "right button released");
    CHECK(sdge_get_mouse_button(w, 77) == 0, "get_mouse_button() out-of-range returns 0");

    /* ===== cursor ===== */
    cur_count = 0; cur_x = -1; cur_y = -1;
    synth_mouse_move(w, 50, 60);
    pump_for(30);
    CHECK(cur_count == 1, "cursor callback fired on move");
    CHECK(cur_x == 50 && cur_y == 60, "cursor callback reports (50, 60)");

    /* warp roundtrip - a human moving the real mouse can interfere.
     * set/get are both synchronous system calls (SetCursorPos + GetCursorPos
     * on Windows, XWarpPointer + XQueryPointer round-trip on X11), so read
     * back IMMEDIATELY: that leaves no window for a human to interfere.
     * Pause only between failed attempts. */
    {
        int warp_ok = 0;
        double cx = -1, cy = -1;
        for (int attempt = 0; attempt < 8 && !warp_ok; ++attempt) {
            sdge_set_cursor_pos(w, 120, 80);
            sdge_get_cursor_pos(w, &cx, &cy);
            if (fabs(cx - 120) <= 3.0 && fabs(cy - 80) <= 3.0) warp_ok = 1;
            else sleep_ms(30);
        }
        CHECK(warp_ok, "set_cursor_pos/get_cursor_pos roundtrip");
        printf("       cursor read back: (%.1f, %.1f)\n", cx, cy);
    }
    {
        double dummy_x = 0, dummy_y = 0;
        sdge_get_cursor_pos(NULL, &dummy_x, &dummy_y); /* must not crash */
    }

    /* ===== scroll ===== */
    scr_count = 0; scr_dx = -999; scr_dy = -999;
    synth_scroll(w, 1.0);
    pump_for(30);
    CHECK(scr_count == 1, "scroll callback fired");
    CHECK(scr_dy == 1.0 && scr_dx == 0.0, "scroll up == (0, +1.0)");
    synth_scroll(w, -1.0);
    pump_for(30);
    CHECK(scr_dy == -1.0, "scroll down == (0, -1.0)");

    /* ===== focus ===== */
    foc_count = 0; foc_state = -1;
    synth_focus(w, 1);
    pump_for(30);
    CHECK(foc_count == 1 && foc_state == 1, "focus callback: focused = 1");
    CHECK(sdge_get_window_attrib(w, SDGE_FOCUSED) == 1, "FOCUSED attrib == 1");

    /* stuck keys must be released on focus loss */
    synth_key(w, 1, 0);
    pump_for(30);
    CHECK(sdge_get_key(w, g_expect_key) == 1, "expected key held before focus loss");
    synth_focus(w, 0);
    pump_for(30);
    CHECK(foc_count == 2 && foc_state == 0, "focus callback: focused = 0");
    CHECK(sdge_get_window_attrib(w, SDGE_FOCUSED) == 0, "FOCUSED attrib == 0");
    CHECK(sdge_get_key(w, g_expect_key) == 0, "held key released on focus loss (no stuck keys)");

    /* ===== close ===== */
    close_count = 0;
    send_close_event(w);
    pump_for(30);
    CHECK(close_count == 1, "close callback fired on WM close");
    CHECK(sdge_window_should_close(w) == true, "should_close == true after WM close");
    sdge_set_window_should_close(w, false);
    CHECK(sdge_window_should_close(w) == false, "set_window_should_close(false) works");

    /* ===== monitor ===== */
    sdge_monitor *mon = sdge_get_primary_monitor();
    int mw = 0, mh = 0;
    sdge_get_monitor_size(mon, &mw, &mh);
    CHECK(mon != NULL, "primary monitor != NULL");
    CHECK(mw > 0 && mh > 0, "primary monitor size > 0");
    CHECK(mw >= 640 && mh >= 480, "primary monitor fits a 640x480 window");

    /* ===== GL context ===== */
    sdge_make_context_current(w);
    sdge_swap_buffers(w);   /* must not crash even without context */
#if !defined(SDGE_NO_GL)
#if defined(SDGE_PLATFORM_WINDOWS)
    CHECK(wglGetCurrentContext() != NULL, "WGL context is current after make_context_current");
#else
    CHECK(glXGetCurrentContext() != NULL, "GLX context is current after make_context_current");
#endif
    const GLubyte *glver = glGetString(GL_VERSION);
    CHECK(glver != NULL, "glGetString(GL_VERSION) works with the created context");
    if (glver) printf("       GL_VERSION = %s\n", (const char *)glver);
#endif

    /* ===== timing ===== */
    uint64_t tv0 = sdge_get_timer_value();
    sleep_ms(30);
    uint64_t tv1 = sdge_get_timer_value();
    CHECK(tv1 > tv0, "get_timer_value() monotonic");
    CHECK(sdge_get_timer_frequency() > 0, "get_timer_frequency() > 0");
    double gt0 = sdge_get_time();
    sleep_ms(30);
    double gt1 = sdge_get_time();
    CHECK(gt1 - gt0 >= 0.025, "get_time() advances after 30ms sleep");
    int64_t ms_now = 0;
    sdge_get_time_ms(&ms_now);
    CHECK(ms_now > 0, "get_time_ms() > 0");
    sdge_get_time_ms(NULL); /* must not crash */

    /* wait_events_timeout returning EARLY is correct when events are pending
     * (same contract as GLFW). DWM/shell can also wake the thread with
     * inter-thread SendMessage bursts (e.g. right after other test processes
     * destroyed many windows). So: drain the queue first, then measure, and
     * accept the check as soon as ONE round demonstrates a full wait. */
    {
        double best = 1e9;
        int saw_full_wait = 0;
        for (int i = 0; i < 8 && !saw_full_wait; ++i) {
            sdge_poll_events();          /* drain: pending input must not shortcut the wait */
            double wt0 = sdge_get_time();
            sdge_wait_events_timeout(0.1);
            double waited = sdge_get_time() - wt0;
            if (waited < best) best = waited;
            if (waited >= 0.09) saw_full_wait = 1;
        }
        CHECK(saw_full_wait, "wait_events_timeout(0.1) can block for the full timeout");
        CHECK(best < 2.0, "wait_events_timeout(0.1) never hangs");
    }

    /* ===== hints: hidden + non-resizable ===== */
    sdge_default_window_hints();
    sdge_window_hint(SDGE_VISIBLE, 0);
    sdge_window_hint(SDGE_RESIZABLE, 0);
    sdge_window *h = sdge_create_window(200, 150, "hidden one", NULL);
    CHECK(h != NULL, "create hidden window");
    CHECK(sdge_get_window_attrib(h, SDGE_VISIBLE) == 0, "VISIBLE hint honored");
    CHECK(sdge_get_window_attrib(h, SDGE_RESIZABLE) == 0, "RESIZABLE=0 hint honored");
#if defined(SDGE_PLATFORM_WINDOWS)
    {
        DWORD st = (DWORD)GetWindowLongW((HWND)sdge_window_get_native_handle(h), GWL_STYLE);
        CHECK((st & WS_THICKFRAME) == 0, "Win32 style lacks WS_THICKFRAME when not resizable");
    }
#else
    {
        Display *dpy = (Display *)sdge_window_get_native_display(h);
        Window xwin = (Window)(uintptr_t)sdge_window_get_native_handle(h);
        Atom motif = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
        Atom type = None; int fmt = 0; unsigned long nitems = 0, after = 0;
        unsigned char *prop = NULL;
        int ok = XGetWindowProperty(dpy, xwin, motif, 0, 32, False, AnyPropertyType,
                                     &type, &fmt, &nitems, &after, &prop);
        CHECK(ok == Success && prop != NULL && nitems >= 5,
              "_MOTIF_WM_HINTS property set on non-resizable window");
        if (prop) XFree(prop);
    }
#endif
    sdge_show_window(h);
    CHECK(sdge_get_window_attrib(h, SDGE_VISIBLE) == 1, "hidden window can be shown later");
    sdge_destroy_window(h);

    /* ===== multiple windows / slot management ===== */
    sdge_default_window_hints();
    sdge_window *w2 = sdge_create_window(300, 200, "second window", w);
    CHECK(w2 != NULL, "second window created (context sharing)");
    int sw = 0, sh = 0;
    sdge_get_window_size(w2, &sw, &sh);
    CHECK(sw == 300 && sh == 200, "second window size == 300x200");
    CHECK(native_title_equals(w2, "second window"), "second window title");
    sdge_destroy_window(w2);

    sdge_window *w3 = sdge_create_window(320, 240, "third window", NULL);
    CHECK(w3 != NULL, "slot is freed after destroy (third window created)");
    sdge_destroy_window(w3);

    /* ===== fullscreen ===== */
    sdge_show_window(w);      /* visible again for the fullscreen path */
    pump_for(50);
    sdge_get_window_size(w, &ww, &wh);          /* 300x200 from the resize test */
    sdge_set_fullscreen(w, true);
    CHECK(sdge_is_fullscreen(w) == true, "is_fullscreen() == true after set_fullscreen(true)");
#if defined(SDGE_PLATFORM_WINDOWS)
    {
        DWORD st = (DWORD)GetWindowLongW((HWND)sdge_window_get_native_handle(w), GWL_STYLE);
        CHECK((st & WS_POPUP) != 0, "fullscreen window uses WS_POPUP style");
        RECT rc;
        if (GetClientRect((HWND)sdge_window_get_native_handle(w), &rc))
            CHECK(rc.right - rc.left == mw && rc.bottom - rc.top == mh,
                  "fullscreen client size covers the monitor");
    }
#endif
    pump_for(100);
    sdge_set_fullscreen(w, false);
    pump_for(50);
    CHECK(sdge_is_fullscreen(w) == false, "is_fullscreen() == false after restore");
#if defined(SDGE_PLATFORM_WINDOWS)
    {
        RECT rc;
        if (GetClientRect((HWND)sdge_window_get_native_handle(w), &rc)) {
            int cw = rc.right - rc.left, ch = rc.bottom - rc.top;
            CHECK(cw == ww && ch == wh, "windowed client size restored after fullscreen");
            if (cw != ww || ch != wh)
                printf("       got %dx%d, expected %dx%d\n", cw, ch, ww, wh);
        }
    }
#endif

    /* ===== window limit ===== */
    sdge_destroy_window(w);
    err_count = 0; err_code = 0;
    {
        sdge_window *many[SDGE_MAX_WINDOWS + 2];
        int created = 0;
        memset(many, 0, sizeof(many));
        for (int i = 0; i < SDGE_MAX_WINDOWS + 2; ++i) {
            many[i] = sdge_create_window(64, 48, "filler", NULL);
            if (!many[i]) break;
            created++;
        }
        CHECK(created == SDGE_MAX_WINDOWS,
              "window limit: exactly SDGE_MAX_WINDOWS windows can exist");
        CHECK(err_count > 0 && err_code == 5,
              "error callback fired with code 5 (too many windows)");
        if (err_count) printf("       last error: %d: %s\n", err_code, err_msg);
        for (int i = 0; i < created; ++i) sdge_destroy_window(many[i]);
    }

    /* ===== terminate ===== */
    sdge_terminate();
    sdge_terminate();   /* idempotency */
    CHECK(true, "sdge_terminate() safe to call twice");

    printf("--\n");
    printf("%d checks, %d failed\n", g_total, g_failed);
    return g_failed;
}
