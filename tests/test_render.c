/* test_render.c - regression test for the forward renderer.
 *
 * Renders a triangle through the engine pipeline (sdge_mesh_upload +
 * sdgr_draw_mesh) in a real window and counts the pixels that reached the
 * back buffer. Catches the classic "everything renders black/invisible"
 * failures (wrong model matrix, lights never applied, broken VAO, ...).
 *
 * Needs an OpenGL context: on Linux the harness runs it under Xvfb.
 *
 * Exit code = number of failed checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "window.h"
#include "graphics/mesh.h"
#include "graphics/renderer.h"
#include "assets/gltf.h"
#include <glad/glad.h>   /* the test reads the framebuffer itself */

#define W 320
#define H 240

static int g_failed = 0, g_total = 0;
#define CHECK(cond, name) do { \
    g_total++; \
    if (cond) printf("  [PASS] %s\n", (name)); \
    else { g_failed++; printf("  [FAIL] %s  (%s:%d)\n", (name), __FILE__, __LINE__); } \
} while (0)

/* counts pixels that are not (nearly) black */
static int count_lit(const unsigned char *px) {
    int n = 0;
    for (int i = 0; i < W * H * 3; i += 3)
        if (px[i] + px[i + 1] + px[i + 2] > 30) n++;
    return n;
}

/* ---- the procedural meshes: the box must be solid from the front ---- */
static void test_primitives(sdge_window *win) {
    printf("\n  -- the procedural meshes --\n");
    sdge_mesh box, sphere;
    CHECK(sdge_mesh_create_box(&box, 0.5f, 0.5f, 0.5f) == 0, "box: created");
    CHECK(sdge_mesh_create_sphere(&sphere, 0.5f, 24, 16) == 0, "sphere: created");
    CHECK(box.index_count == 36, "box: 36 indices");
    CHECK(sphere.index_count > 0, "sphere: has indices");

    /* a box at the origin, the camera at +Z: the front face must be lit
     * (an inverted winding would cull it and leave a hole) */
    const float id[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

    for (int which = 0; which < 2; ++which) {
        const sdge_mesh *m = which ? &sphere : &box;
        unsigned char *px = (unsigned char *)malloc((size_t)W * H * 3u);
        memset(px, 0, (size_t)W * H * 3u);
        for (int f = 0; f < 3; ++f) {
            sdge_poll_events();
            sdgr_resize(W, H);
            sdgr_begin_frame((const float[]){ 0.0f, 0.0f, 0.0f, 1.0f });
            sdgr_set_camera(id, id, (const float[3]){ 0.0f, 0.0f, 3.0f });
            sdgr_draw_mesh(m, NULL, NULL, 0, white, 0.0f, 0.5f, 0, 0);
            if (f == 1) {
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px);
            }
            sdge_swap_buffers(win);
        }
        int cx = W / 2, cy = H / 2;
        int o = (cy * W + cx) * 3;
        int lit = px[o] + px[o + 1] + px[o + 2];
        printf("        the %s centre pixel: (%d,%d,%d)\n",
               which ? "sphere" : "box", px[o], px[o + 1], px[o + 2]);
        CHECK(lit > 60, which ? "sphere: the centre is lit"
                              : "box: the centre is lit (not culled)");
        free(px);
    }

    sdge_mesh_destroy(&box);
    sdge_mesh_destroy(&sphere);
}

int main(void) {
    if (!sdge_init()) { fprintf(stderr, "engine init failed\n"); return 1; }
    sdge_window *win = sdge_create_window(W, H, "sdge render test", NULL);
    if (!win) { fprintf(stderr, "window creation failed\n"); return 1; }
    sdge_show_window(win);
    sdge_make_context_current(win);

    CHECK(sdgr_init() == 0, "renderer: init");
    if (sdgr_init() != 0) { sdge_destroy_window(win); sdge_terminate(); return 1; }

    /* a triangle in the XY plane, facing +Z */
    static const float pos[9] = {
        -0.8f, -0.8f, 0.0f,
         0.8f, -0.8f, 0.0f,
         0.0f,  0.8f, 0.0f,
    };
    static const float nrm[9] = { 0,0,1, 0,0,1, 0,0,1 };
    static const unsigned short idx[3] = { 0, 1, 2 };

    sdge_primitive prim;
    memset(&prim, 0, sizeof(prim));
    prim.positions.data = (const unsigned char *)pos;
    prim.positions.count = 3; prim.positions.comps = 3;
    prim.positions.ctype = SDGE_GLTF_FLOAT; prim.positions.stride = 12;
    prim.normals.data = (const unsigned char *)nrm;
    prim.normals.count = 3; prim.normals.comps = 3;
    prim.normals.ctype = SDGE_GLTF_FLOAT; prim.normals.stride = 12;
    prim.indices.data = (const unsigned char *)idx;
    prim.indices.count = 3; prim.indices.comps = 1;
    prim.indices.ctype = SDGE_GLTF_UNSIGNED_SHORT; prim.indices.stride = 2;
    prim.material = -1; prim.mode = 4;

    sdge_mesh mesh;
    int rc = sdge_mesh_upload(&mesh, &prim, NULL);
    CHECK(rc == 0, "mesh: upload");
    CHECK(mesh.vao != 0, "mesh: vao created");
    CHECK(mesh.index_count == 3 && mesh.ebo != 0, "mesh: index buffer created");

    unsigned char *px = (unsigned char *)malloc((size_t)W * H * 3u);
    const float id[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
    float clear[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    memset(px, 0, (size_t)W * H * 3u);
    for (int f = 0; f < 3; ++f) {
        sdge_poll_events();
        sdgr_resize(W, H);
        sdgr_begin_frame(clear);
        sdgr_set_camera(id, id, (const float[3]){ 0.0f, 0.0f, 5.0f });
        sdgr_draw_mesh(&mesh, NULL, NULL, 0, NULL, 0.0f, 0.6f, 0, 0);
        if (f == 1) {
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px);
        }
        sdge_swap_buffers(win);
    }

    int lit = count_lit(px);
    printf("        lit pixels: %d / %d\n", lit, W * H);
    /* the triangle covers roughly a third of the window */
    CHECK(lit > 5000, "renderer: triangle is visible");
    CHECK(lit < W * H, "renderer: background is still cleared");

    /* ---- the same through a real glTF file (skinned character) ---- */
    {
        sdge_gltf_model *m = sdge_load_gltf("Subjects/character.glb");
        CHECK(m != NULL, "gltf: character.glb loads");
        if (m && m->mesh_count > 0 && m->meshes[0].prim_count > 0) {
            sdge_mesh cm;
            memset(&cm, 0, sizeof(cm));
            /* the material matters: a double-sided one disables culling */
            const sdge_gltf_material *mat = NULL;
            int mi = m->meshes[0].prims[0].material;
            if (mi >= 0 && mi < m->material_count) mat = &m->materials[mi];
            int rc2 = sdge_mesh_upload(&cm, &m->meshes[0].prims[0], mat);
            CHECK(rc2 == 0, "mesh: character upload");
            CHECK(cm.has_skin == 1 || cm.has_normals == 1,
                  "mesh: character carries skin/normal attributes");

            /* camera in front of the character (fovy is in RADIANS) */
            float view[16], proj[16];
            m4_look_at(view, (float[3]){ 0.0f, 1.1f, 3.0f },
                       (float[3]){ 0.0f, 1.0f, 0.0f },
                       (float[3]){ 0.0f, 1.0f, 0.0f });
            m4_perspective(proj, 50.0f * 3.14159265f / 180.0f,
                           (float)W / (float)H, 0.1f, 100.0f);

            memset(px, 0, (size_t)W * H * 3u);
            for (int f = 0; f < 3; ++f) {
                sdge_poll_events();
                sdgr_resize(W, H);
                sdgr_begin_frame(clear);
                sdgr_set_camera(view, proj, (const float[3]){ 0.0f, 1.1f, 3.0f });
                sdgr_draw_mesh(&cm, NULL, NULL, 0, NULL, 0.0f, 0.6f, 0, 0);
                if (f == 1) {
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(0, 0, W, H, GL_RGB, GL_UNSIGNED_BYTE, px);
                }
                sdge_swap_buffers(win);
            }
            int lit2 = count_lit(px);
            printf("        character lit pixels: %d / %d\n", lit2, W * H);
            CHECK(lit2 > 200, "renderer: character is visible");

            sdge_mesh_destroy(&cm);
        }
        if (m) sdge_gltf_destroy(m);
    }

    free(px);
    sdge_mesh_destroy(&mesh);
    sdgr_shutdown();
    sdge_destroy_window(win);
    sdge_terminate();

    /* the primitives need their own context */
    {
        sdge_window *w2 = sdge_create_window(W, H, "primitives", NULL);
        if (w2) {
            sdge_show_window(w2);
            sdge_make_context_current(w2);
            if (sdgr_init() == 0) test_primitives(w2);
            sdgr_shutdown();
            sdge_destroy_window(w2);
        }
    }

    printf("\ntest_render: %d checks, %d failed\n", g_total, g_failed);
    return g_failed;
}
