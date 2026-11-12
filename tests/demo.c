/* tests/demo.c - manual smoke test: animated GL window, ESC to quit.
 * Build and run, then verify by hand:
 *   - window appears with the given title
 *   - background cycles colors
 *   - ESC closes the window
 *   - FPS shown in the title bar
 */
#include <stdio.h>
#include <math.h>

#define SDGE_WINDOW_IMPLEMENTATION
#include "../core/graphics/window.h"

static void on_key(sdge_window *w, int key, int scancode, int action, int mods) {
    (void)scancode; (void)mods;
    if (key == SDGE_KEY_ESCAPE && action == SDGE_PRESS)
        sdge_set_window_should_close(w, true);
}

static void on_size(sdge_window *w, int width, int height) {
    (void)w;
    printf("resized to %dx%d\n", width, height);
}

int main(void) {
    if (!sdge_init()) return 1;

    sdge_window *w = sdge_create_window(800, 600, "Silver Diamond demo - ESC to quit", NULL);
    if (!w) { fprintf(stderr, "window creation failed\n"); return 1; }

    sdge_set_key_callback(w, on_key);
    sdge_set_window_size_callback(w, on_size);
    sdge_make_context_current(w);

    double t0 = sdge_get_time();
    int frames = 0;
    char title[160];

    while (!sdge_window_should_close(w)) {
        sdge_poll_events();

#ifndef SDGE_NO_GL
        double t = sdge_get_time();
        glClearColor((float)(0.5 + 0.5 * sin(t)),
                     (float)(0.5 + 0.5 * sin(t + 2.1)),
                     (float)(0.5 + 0.5 * sin(t + 4.2)),
                     1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
#endif
        sdge_swap_buffers(w);
        frames++;

        double dt = sdge_get_time() - t0;
        if (dt >= 0.5) {
            sprintf(title, "Silver Diamond demo - %.0f FPS", frames / dt);
            sdge_set_window_title(w, title);
            frames = 0;
            t0 = sdge_get_time();
        }
    }

    sdge_destroy_window(w);
    sdge_terminate();
    printf("demo finished cleanly\n");
    return 0;
}
