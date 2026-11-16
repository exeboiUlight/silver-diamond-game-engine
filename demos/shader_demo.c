/* demos/shader_demo.c - Silver Diamond Game Engine :: custom shader demo
 *
 * Shows the small shader language of the engine:
 *
 *     python build.py editor
 *     python build.py demo --demo shader_demo
 *     build/windows/release/shader_demo.exe [model.glb] [--shader file]
 *
 * Without a model the demo draws a generated sphere, so it works with no
 * assets at all.  The shader uniforms are animated from C with
 * sdgr_shader_set_float() / sdgr_shader_set_vec3() / sdgr_shader_set_time().
 */
#include <sdge.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_running = 1;

static void on_key(sdge_window *w, int key, int scancode, int action, int mods) {
    (void)w; (void)scancode; (void)mods;
    if (action == SDGE_PRESS && key == SDGE_KEY_ESCAPE) g_running = 0;
}

int main(int argc, char **argv) {
    const char *model_path  = NULL;
    const char *shader_path = "demos/glow.sdshader";
    int frame_limit = 0;
    int shot_frame = 0;
    const char *shot_path = NULL;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--shader") && i + 1 < argc) {
            shader_path = argv[++i];
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--shot") && i + 2 < argc) {
            shot_frame = atoi(argv[++i]);
            shot_path = argv[++i];
        } else {
            model_path = argv[i];
        }
    }

    if (!sdge_init()) {
        fprintf(stderr, "shader_demo: engine init failed\n");
        return 1;
    }
    sdge_window_hint(SDGE_VSYNC, 0);
    sdge_window *win = sdge_create_window(1280, 720, "SDGE - shader demo", NULL);
    if (!win) {
        fprintf(stderr, "shader_demo: window creation failed\n");
        sdge_terminate();
        return 1;
    }
    int width = 1, height = 1;
    sdge_get_framebuffer_size(win, &width, &height);
    sdge_show_window(win);
    sdge_make_context_current(win);
    sdge_set_key_callback(win, on_key);

    if (sdgr_init() != 0) {
        fprintf(stderr, "shader_demo: the renderer is not available\n");
        sdge_destroy_window(win);
        sdge_terminate();
        return 1;
    }

    char error[1024];
    sdgr_shader *shader = sdgr_shader_load_file(shader_path, error, sizeof(error));
    if (!shader) {
        fprintf(stderr, "shader_demo: %s\n%s\n", shader_path, error);
        sdgr_shutdown();
        sdge_destroy_window(win);
        sdge_terminate();
        return 1;
    }
    printf("shader_demo: loaded '%s'\n", shader_path);

    sdge_model *model = model_path ? sdge_model_load(model_path) : NULL;
    if (model_path && !model) {
        fprintf(stderr, "shader_demo: cannot load '%s'\n", model_path);
        sdgr_shader_destroy(shader);
        sdgr_shutdown();
        sdge_destroy_window(win);
        sdge_terminate();
        return 1;
    }
    if (model) {
        printf("shader_demo: model '%s' with the custom shader\n", model_path);
        sdge_model_set_shader(model, shader);
        if (sdge_model_anim_count(model) > 0) sdge_model_play(model, 0, 1);
    } else {
        printf("shader_demo: no model, drawing a generated sphere\n");
    }

    sdge_mesh sphere;
    memset(&sphere, 0, sizeof(sphere));
    if (!model) sdge_mesh_create_sphere(&sphere, 1.0f, 64, 32);

    sdge_camera3d cam;
    sdge_camera3d_init(&cam, 45.0f,
                       (float)width / (float)(height ? height : 1),
                       0.05f, 200.0f);

    sdgr_set_ambient(0.16f, 0.17f, 0.20f);
    {
        float dir[3]   = { -0.4f, -1.0f, -0.35f };
        float color[3] = { 1.0f, 0.96f, 0.90f };
        sdgr_set_sun(dir, color, 2.4f);
    }

    sdgr_material mat;
    memset(&mat, 0, sizeof(mat));
    mat.base_color[0] = mat.base_color[1] = mat.base_color[2] = 0.85f;
    mat.base_color[3] = 1.0f;
    mat.metallic = 0.1f;
    mat.roughness = 0.55f;
    mat.normal_scale = 1.0f;
    mat.occlusion_strength = 1.0f;
    mat.alpha_mode = SDGR_ALPHA_OPAQUE;
    mat.alpha_cutoff = 0.5f;
    for (int i = 0; i < SDGR_TEX_SLOTS; ++i) {
        mat.uv_scale[i][0] = 1.0f;
        mat.uv_scale[i][1] = 1.0f;
    }

    float center[3] = { 0.0f, 0.0f, 0.0f };
    float dist = model ? 4.0f : 3.2f;
    float yaw = 0.6f, pitch = 0.22f;
    float elapsed = 0.0f;
    int frames = 0;

    while (g_running && !sdge_window_should_close(win)) {
        sdge_poll_events();
        double now = sdge_get_time();
        static double last = 0.0;
        if (last == 0.0) last = now;
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;
        elapsed += dt;
        yaw += dt * 0.35f;

        if (model) sdge_model_advance(model, dt);

        /* animate the user uniforms of the shader */
        float pulse = 0.55f + 0.45f * sinf(elapsed * 2.0f);
        sdgr_shader_set_time(shader, elapsed);
        sdgr_shader_set_float(shader, "intensity", pulse);
        sdgr_shader_set_float(shader, "speed", 1.4f);
        sdgr_shader_set_vec3(shader, "tint", 0.20f, 0.75f, 1.0f);

        sdge_camera3d_orbit(&cam, center, dist, yaw, pitch);
        sdge_camera3d_update(&cam);
        sdgr_set_camera(cam.view, cam.proj, cam.pos);

        sdge_get_framebuffer_size(win, &width, &height);
        sdgr_resize(width, height);
        float clear[4] = { 0.04f, 0.05f, 0.07f, 1.0f };
        sdgr_begin_frame(clear);

        if (model) {
            sdge_model_draw(model, NULL);
        } else {
            sdg_mat4 world;
            m4_identity(world);
            sdgr_draw_mesh_shader(&sphere, world, NULL, 0, &mat, NULL, shader);
        }

        sdgr_end_frame();
        sdge_swap_buffers(win);

        frames++;
        if (shot_path && frames == shot_frame) {
            if (sdgr_screenshot(shot_path) == 0)
                printf("shader_demo: screenshot written to %s\n", shot_path);
        }
        if (frame_limit > 0 && frames >= frame_limit) break;
    }

    if (!model) sdge_mesh_destroy(&sphere);
    if (model) sdge_model_destroy(model);
    sdgr_shader_destroy(shader);
    sdgr_shutdown();
    sdge_destroy_window(win);
    sdge_terminate();
    return 0;
}
