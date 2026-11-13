/* demos/texture_demo.c - Silver Diamond Game Engine :: texturing showcase
 *
 * Loads a glTF/GLB model and renders it with the full PBR texture pipeline
 * (base color + metallic-roughness + normal + occlusion + emissive, sRGB
 * decoding, alpha modes, samplers, UV transforms).
 *
 *   texture_demo [model.glb|model.gltf] [--shot N file.ppm] [--frames N]
 *
 * Mouse drag = orbit, wheel = zoom, SPACE = play/pause the animation,
 * 1..9 = pick the n-th animation, ESC = quit.
 */
#include <sdge.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    sdge_window *win;
    int width, height;
    sdge_camera3d cam;
    sdge_model *model;

    float yaw, pitch, dist;
    float center[3];
    int   dragging;
    double last_x, last_y;

    int   anim;
    int   playing;
    int   frames;
    int   shot_frame;
    const char *shot_path;
    int   done;
} demo;

static demo g;

static void on_mouse_button(sdge_window *w, int button, int action, int mods) {
    (void)w; (void)mods;
    if (button == SDGE_MOUSE_BUTTON_LEFT) {
        if (action == SDGE_PRESS) {
            g.dragging = 1;
            sdge_get_cursor_pos(g.win, &g.last_x, &g.last_y);
        } else {
            g.dragging = 0;
        }
    }
}

static void on_cursor(sdge_window *w, double x, double y) {
    (void)w;
    if (!g.dragging) return;
    g.yaw   -= (float)(x - g.last_x) * 0.01f;
    g.pitch += (float)(y - g.last_y) * 0.01f;
    if (g.pitch >  1.5f) g.pitch =  1.5f;
    if (g.pitch < -1.5f) g.pitch = -1.5f;
    g.last_x = x;
    g.last_y = y;
}

static void on_scroll(sdge_window *w, double dx, double dy) {
    (void)w; (void)dx;
    g.dist -= (float)dy * 0.01f;
    if (g.dist < 0.2f)  g.dist = 0.2f;
    if (g.dist > 200.0f) g.dist = 200.0f;
}

static void on_key(sdge_window *w, int key, int scancode, int action, int mods) {
    (void)w; (void)scancode; (void)mods;
    if (action != SDGE_PRESS) return;   /* only on press */
    if (key == SDGE_KEY_ESCAPE) {
        sdge_set_window_should_close(g.win, 1);
    } else if (key == SDGE_KEY_SPACE) {
        g.playing = !g.playing;
    } else if (key >= SDGE_KEY_1 && key <= SDGE_KEY_9) {
        int idx = key - SDGE_KEY_1;
        if (g.model && idx < sdge_model_anim_count(g.model)) {
            sdge_model_play(g.model, idx, 1);
            g.anim = idx;
            g.playing = 1;
        }
    }
}

/* computes the bounding sphere of the model (accessor bounds + node
 * transforms) so the camera can frame it automatically */
static void frame_model(sdge_model *model, float center[3], float *radius) {
    const sdge_gltf_model *gltf = sdge_model_gltf(model);
    center[0] = center[1] = center[2] = 0.0f;
    *radius = 1.0f;
    if (!gltf || gltf->node_count <= 0) return;

    sdg_mat4 *world = (sdg_mat4 *)calloc((size_t)gltf->node_count,
                                         sizeof(sdg_mat4));
    if (!world) return;
    for (int i = 0; i < gltf->node_count; ++i) {
        int parent = gltf->nodes[i].parent;
        if (parent >= 0 && parent != i)
            m4_mul(world[i], world[parent], gltf->nodes[i].matrix);
        else
            m4_copy(world[i], gltf->nodes[i].matrix);
    }

    float mn[3] = { 1e30f, 1e30f, 1e30f };
    float mx[3] = { -1e30f, -1e30f, -1e30f };
    int found = 0;
    for (int i = 0; i < gltf->node_count; ++i) {
        if (gltf->nodes[i].mesh < 0) continue;
        const sdge_gltf_mesh *mesh = &gltf->meshes[gltf->nodes[i].mesh];
        for (int p = 0; p < mesh->prim_count; ++p) {
            const sdge_accessor *pa = &mesh->prims[p].positions;
            if (pa->count <= 0) continue;
            for (int c = 0; c < 8; ++c) {
                sdg_vec3 corner, out;
                corner[0] = (c & 1) ? pa->bounds_max[0] : pa->bounds_min[0];
                corner[1] = (c & 2) ? pa->bounds_max[1] : pa->bounds_min[1];
                corner[2] = (c & 4) ? pa->bounds_max[2] : pa->bounds_min[2];
                m4_mul_point(out, world[i], corner);
                for (int k = 0; k < 3; ++k) {
                    if (out[k] < mn[k]) mn[k] = out[k];
                    if (out[k] > mx[k]) mx[k] = out[k];
                }
                found = 1;
            }
        }
    }
    free(world);
    if (!found) return;
    for (int k = 0; k < 3; ++k) center[k] = (mn[k] + mx[k]) * 0.5f;
    float r = 0.0f;
    for (int k = 0; k < 3; ++k) {
        float d = (mx[k] - mn[k]) * 0.5f;
        r += d * d;
    }
    *radius = sqrtf(r);
}

static void print_model_info(sdge_model *model) {
    const sdge_gltf_model *gltf = sdge_model_gltf(model);
    if (!gltf) return;

    printf("model: %d nodes, %d meshes, %d materials, %d textures, "
           "%d animations\n",
           gltf->node_count, gltf->mesh_count, gltf->material_count,
           gltf->texture_count, gltf->anim_count);

    for (int i = 0; i < gltf->material_count; ++i) {
        const sdge_gltf_material *m = &gltf->materials[i];
        printf("  material %d '%s': base(%.2f %.2f %.2f %.2f) metal %.2f "
               "rough %.2f alpha %d cutoff %.2f emissive(%.2f %.2f %.2f)\n",
               i, m->name ? m->name : "-",
               m->base_color[0], m->base_color[1], m->base_color[2],
               m->base_color[3], m->metallic, m->roughness,
               m->alpha_mode, m->alpha_cutoff,
               m->emissive[0], m->emissive[1], m->emissive[2]);
        printf("      tex: base=%d mr=%d normal=%d occl=%d emissive=%d"
               " (normal scale %.2f, occl strength %.2f)\n",
               m->base_color_tex.index, m->metallic_roughness_tex.index,
               m->normal_tex.index, m->occlusion_tex.index,
               m->emissive_tex.index,
               m->normal_tex.scale, m->occlusion_tex.scale);
    }
    for (int i = 0; i < gltf->texture_count; ++i) {
        const sdge_gltf_texture *t = &gltf->textures[i];
        const char *src = "-";
        if (t->source >= 0 && t->source < gltf->image_count) {
            const sdge_gltf_image *im = &gltf->images[t->source];
            if (im->uri) src = im->uri;
            else if (im->data) src = "<data-uri>";
            else src = "<bufferView>";
        }
        printf("  texture %d: sampler %d, image %d (%s)\n",
               i, t->sampler, t->source, src);
    }
}

int main(int argc, char **argv) {
    const char *model_path = "assets/scene.gltf";
    int frame_limit = 0;
    float opt_dist = -1.0f, opt_yaw = 0.0f, opt_pitch = 0.0f;
    int opt_yaw_set = 0, opt_pitch_set = 0;
    float opt_ambient[3] = { 0.18f, 0.19f, 0.22f };
    float opt_sun_i = 2.4f;
    float opt_target[3] = { 0.0f, 0.0f, 0.0f };
    int opt_target_set = 0;

    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--shot") && i + 2 < argc) {
            g.shot_frame = atoi(argv[i + 1]);
            g.shot_path = argv[i + 2];
            i += 2;
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frame_limit = atoi(argv[i + 1]);
            i += 1;
        } else if (!strcmp(argv[i], "--dist") && i + 1 < argc) {
            opt_dist = (float)atof(argv[i + 1]);
            i += 1;
        } else if (!strcmp(argv[i], "--yaw") && i + 1 < argc) {
            opt_yaw = (float)atof(argv[i + 1]);
            opt_yaw_set = 1;
            i += 1;
        } else if (!strcmp(argv[i], "--pitch") && i + 1 < argc) {
            opt_pitch = (float)atof(argv[i + 1]);
            opt_pitch_set = 1;
            i += 1;
        } else if (!strcmp(argv[i], "--target") && i + 1 < argc) {
            if (sscanf(argv[i + 1], "%f,%f,%f",
                       &opt_target[0], &opt_target[1], &opt_target[2]) == 3)
                opt_target_set = 1;
            i += 1;
        } else if (!strcmp(argv[i], "--ambient") && i + 1 < argc) {
            if (sscanf(argv[i + 1], "%f,%f,%f",
                       &opt_ambient[0], &opt_ambient[1], &opt_ambient[2]) != 3)
                opt_ambient[0] = opt_ambient[1] = opt_ambient[2] = 0.2f;
            i += 1;
        } else if (!strcmp(argv[i], "--sun") && i + 1 < argc) {
            opt_sun_i = (float)atof(argv[i + 1]);
            i += 1;
        } else {
            model_path = argv[i];
        }
    }

    if (!sdge_init()) {
        fprintf(stderr, "texture_demo: engine init failed\n");
        return 1;
    }
    sdge_window_hint(SDGE_VSYNC, 0);
    g.win = sdge_create_window(1280, 720, "SDGE - texturing demo", NULL);
    if (!g.win) {
        fprintf(stderr, "texture_demo: window creation failed\n");
        sdge_terminate();
        return 1;
    }
    sdge_get_framebuffer_size(g.win, &g.width, &g.height);
    sdge_show_window(g.win);
    sdge_make_context_current(g.win);
    sdge_set_mouse_button_callback(g.win, on_mouse_button);
    sdge_set_cursor_pos_callback(g.win, on_cursor);
    sdge_set_scroll_callback(g.win, on_scroll);
    sdge_set_key_callback(g.win, on_key);

    if (sdgr_init() != 0) {
        fprintf(stderr, "texture_demo: the renderer is not available\n");
        sdge_destroy_window(g.win);
        sdge_terminate();
        return 1;
    }

    g.model = sdge_model_load(model_path);
    if (!g.model) {
        fprintf(stderr, "texture_demo: cannot load '%s'\n", model_path);
        sdge_destroy_window(g.win);
        sdge_terminate();
        return 1;
    }
    print_model_info(g.model);

    if (sdge_model_anim_count(g.model) > 0) {
        sdge_model_play(g.model, 0, 1);
        g.playing = 1;
    }

    /* the camera looks at the middle of the model */
    {
        float radius = 1.0f;
        frame_model(g.model, g.center, &radius);
        g.dist = radius / tanf(0.5f * 45.0f * SDG_PI / 180.0f) * 1.25f;
        if (g.dist < 0.5f) g.dist = 0.5f;
        printf("texture_demo: model radius %.3f, camera distance %.3f\n",
               radius, g.dist);
    }
    if (opt_target_set) {
        g.center[0] = opt_target[0];
        g.center[1] = opt_target[1];
        g.center[2] = opt_target[2];
    }
    if (opt_dist > 0.0f) g.dist = opt_dist;
    g.yaw = opt_yaw_set ? opt_yaw : 0.7f;
    g.pitch = opt_pitch_set ? opt_pitch : 0.25f;
    sdge_camera3d_init(&g.cam, 45.0f,
                       (float)g.width / (float)(g.height ? g.height : 1),
                       0.05f, 500.0f);

    sdgr_set_ambient(opt_ambient[0], opt_ambient[1], opt_ambient[2]);
    {
        float dir[3]  = { -0.4f, -1.0f, -0.35f };
        float color[3] = { 1.0f, 0.96f, 0.90f };
        sdgr_set_sun(dir, color, opt_sun_i);
    }
    {
        float pos[3] = { 2.5f, 2.0f, 2.5f };
        float col[3] = { 1.0f, 0.8f, 0.6f };
        sdgr_set_point_light(0, pos, col, 1.5f, 12.0f);
    }

    double last = sdge_get_time();
    while (!sdge_window_should_close(g.win)) {
        sdge_poll_events();
        double now = sdge_get_time();
        float dt = (float)(now - last);
        last = now;
        if (dt > 0.1f) dt = 0.1f;

        if (g.playing) sdge_model_advance(g.model, dt);

        sdge_camera3d_orbit(&g.cam, g.center, g.dist, g.yaw, g.pitch);
        sdge_camera3d_update(&g.cam);
        sdgr_set_camera(g.cam.view, g.cam.proj, g.cam.pos);

        sdgr_resize(g.width, g.height);
        {
            float clear[4] = { 0.06f, 0.07f, 0.09f, 1.0f };
            sdgr_begin_frame(clear);
        }
        sdge_model_draw(g.model, NULL);
        sdgr_end_frame();
        sdge_swap_buffers(g.win);

        g.frames++;
        if (g.shot_path && g.frames == g.shot_frame) {
            sdgr_screenshot(g.shot_path);
            printf("texture_demo: screenshot written to %s\n", g.shot_path);
        }
        if (frame_limit > 0 && g.frames >= frame_limit) break;
    }

    sdge_model_destroy(g.model);
    sdgr_shutdown();
    sdge_destroy_window(g.win);
    sdge_terminate();
    return 0;
}
