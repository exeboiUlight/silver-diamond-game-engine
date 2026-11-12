/* model.c - Silver Diamond Game Engine :: 3D model subject implementation */
#include "model.h"
#include "../../graphics/renderer.h"
#include "../../assets/texture.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#ifndef SDGE_NO_GL
#include <stb_image.h>
#endif

struct sdge_model {
    sdge_gltf_model *gltf;
    sdge_mesh **prims;

    /* NEW: GL textures, parallel to gltf->textures (0 = none) */
    unsigned *textures;

    sdge_anim_player **players;
    int player_count;
    sdge_anim_player *active;

    sdg_mat4 *static_world;
    float   **static_palette;
};

static void m_world_from_locals(const sdge_gltf_model *g,
                                const sdg_mat4 *locals,
                                sdg_mat4 *world) {
    int n = g->node_count;
    if (n <= 0) return;

    int *depth = (int *)malloc(sizeof(int) * (size_t)n);
    int *order = (int *)malloc(sizeof(int) * (size_t)n);
    if (!depth || !order) { free(depth); free(order); return; }

    for (int i = 0; i < n; ++i) {
        int p = g->nodes[i].parent, d = 0;
        while (p >= 0 && p != i && d < n) { p = g->nodes[p].parent; ++d; }
        depth[i] = d;
        order[i] = i;
    }
    for (int i = 1; i < n; ++i) {
        int ni = order[i];
        int k = i - 1;
        while (k >= 0 && depth[order[k]] > depth[ni]) { order[k + 1] = order[k]; --k; }
        order[k + 1] = ni;
    }
    for (int k = 0; k < n; ++k) {
        int i = order[k];
        int parent = g->nodes[i].parent;
        if (parent >= 0 && parent != i && depth[parent] < depth[i])
            m4_mul(world[i], world[parent], locals[i]);
        else
            m4_copy(world[i], locals[i]);
    }
    free(depth);
    free(order);
}

/* NEW: resolve a glTF image into RGBA8 (external file or GLB bufferView) */
#ifndef SDGE_NO_GL
static int load_image_rgba(const sdge_gltf_model *g,
                           const sdge_gltf_image *im,
                           const char *gltf_path,
                           sdga_image *out) {
    memset(out, 0, sizeof(*out));

    if (im->uri && !strncmp(im->uri, "data:", 5)) return -1;
    if (im->uri) {
        char path[1024];
        const char *slash = strrchr(gltf_path, '/');
        const char *bslash = strrchr(gltf_path, '\\');
        const char *sep = (slash && (!bslash || slash > bslash)) ? slash : bslash;
        if (sep) {
            size_t dir = (size_t)(sep - gltf_path) + 1;
            if (dir + strlen(im->uri) + 1 > sizeof(path)) return -1;
            memcpy(path, gltf_path, dir);
            strcpy(path + dir, im->uri);
        } else {
            if (strlen(im->uri) + 1 > sizeof(path)) return -1;
            strcpy(path, im->uri);
        }
        return sdga_image_load_rgba(path, out);
    }

    if (im->buffer_view < 0 || im->buffer_view >= g->buffer_view_count) return -1;
    const sdge_gltf_buffer_view *bv = &g->buffer_views[im->buffer_view];
    if (!bv->data || bv->byte_length <= 0) return -1;

    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char *pixels = stbi_load_from_memory(bv->data, bv->byte_length,
                                                   &w, &h, &ch, 4);
    if (!pixels || w <= 0 || h <= 0) { if (pixels) stbi_image_free(pixels); return -2; }
    out->w = w; out->h = h; out->channels = 4; out->pixels = pixels;
    return 0;
}
#endif

SDGE_API sdge_model *sdge_model_load(const char *path) {
    if (!path) return NULL;
    sdge_gltf_model *g = sdge_load_gltf(path);
    if (!g) return NULL;

    sdge_model *m = (sdge_model *)calloc(1, sizeof(*m));
    if (!m) { sdge_gltf_destroy(g); return NULL; }
    m->gltf = g;

    if (g->mesh_count > 0) {
        m->prims = (sdge_mesh **)calloc((size_t)g->mesh_count, sizeof(*m->prims));
        if (!m->prims) { sdge_model_destroy(m); return NULL; }
        for (int i = 0; i < g->mesh_count; ++i) {
            sdge_gltf_mesh *mesh = &g->meshes[i];
            if (mesh->prim_count <= 0) continue;
            sdge_mesh *pm = (sdge_mesh *)calloc((size_t)mesh->prim_count,
                                                 sizeof(sdge_mesh));
            if (!pm) { sdge_model_destroy(m); return NULL; }
            m->prims[i] = pm;
            for (int p = 0; p < mesh->prim_count; ++p) {
                const sdge_gltf_material *mat =
                    (mesh->prims[p].material >= 0
                     && mesh->prims[p].material < g->material_count)
                        ? &g->materials[mesh->prims[p].material] : NULL;
                if (sdge_mesh_upload(&pm[p], &mesh->prims[p], mat) != 0)
                    fprintf(stderr, "model: mesh %d prim %d failed to upload\n",
                            i, p);
            }
        }
    }

    /* NEW: upload all textures */
    if (g->texture_count > 0) {
        m->textures = (unsigned *)calloc((size_t)g->texture_count,
                                          sizeof(unsigned));
        if (!m->textures) { sdge_model_destroy(m); return NULL; }
#ifndef SDGE_NO_GL
        for (int i = 0; i < g->texture_count; ++i) {
            const sdge_gltf_texture *tx = &g->textures[i];
            if (tx->source < 0 || tx->source >= g->image_count) continue;
            sdga_image img;
            if (load_image_rgba(g, &g->images[tx->source], path, &img) != 0) {
                fprintf(stderr, "model: texture %d image load failed\n", i);
                continue;
            }
            m->textures[i] = sdgr_upload_texture(img.w, img.h, img.pixels);
            sdga_image_free(&img);
        }
#endif
    }

    m->player_count = g->anim_count;
    if (m->player_count > 0) {
        m->players = (sdge_anim_player **)calloc((size_t)m->player_count,
                                                 sizeof(*m->players));
        if (!m->players) { sdge_model_destroy(m); return NULL; }
    }

    if (g->node_count > 0 && g->nodes) {
        m->static_world = (sdg_mat4 *)calloc((size_t)g->node_count,
                                             sizeof(sdg_mat4));
        if (!m->static_world) { sdge_model_destroy(m); return NULL; }
        sdg_mat4 *locals = (sdg_mat4 *)malloc(sizeof(sdg_mat4)
                                              * (size_t)g->node_count);
        if (!locals) { sdge_model_destroy(m); return NULL; }
        for (int i = 0; i < g->node_count; ++i)
            m4_copy(locals[i], g->nodes[i].matrix);
        m_world_from_locals(g, locals, m->static_world);
        free(locals);

        if (g->skin_count > 0) {
            m->static_palette = (float **)calloc((size_t)g->skin_count,
                                                  sizeof(float *));
            if (!m->static_palette) { sdge_model_destroy(m); return NULL; }
            for (int s = 0; s < g->skin_count; ++s) {
                int jc = g->skins[s].joint_count;
                if (jc <= 0) continue;
                float *pal = (float *)malloc(sizeof(float) * 16u * (size_t)jc);
                if (!pal) { sdge_model_destroy(m); return NULL; }
                for (int j = 0; j < jc; ++j) {
                    int node = g->skins[s].joints[j];
                    sdg_mat4 world, pal_m;
                    if (node >= 0 && node < g->node_count)
                        m4_copy(world, m->static_world[node]);
                    else
                        m4_identity(world);
                    if (g->skins[s].inverse_bind) {
                        sdg_mat4 ibm;
                        memcpy(ibm, g->skins[s].inverse_bind + (size_t)j * 16,
                               sizeof(ibm));
                        m4_mul(pal_m, world, ibm);
                    } else {
                        m4_copy(pal_m, world);
                    }
                    memcpy(pal + (size_t)j * 16, pal_m, sizeof(pal_m));
                }
                m->static_palette[s] = pal;
            }
        }
    }
    return m;
}

SDGE_API void sdge_model_destroy(sdge_model *m) {
    if (!m) return;
    if (m->prims) {
        for (int i = 0; i < m->gltf->mesh_count; ++i) {
            if (!m->prims[i]) continue;
            for (int p = 0; p < m->gltf->meshes[i].prim_count; ++p)
                sdge_mesh_destroy(&m->prims[i][p]);
            free(m->prims[i]);
        }
        free(m->prims);
    }
    /* NEW: free GL textures */
    if (m->textures) {
        for (int i = 0; i < m->gltf->texture_count; ++i)
            if (m->textures[i]) sdgr_free_texture(m->textures[i]);
        free(m->textures);
    }
    if (m->players) {
        for (int i = 0; i < m->player_count; ++i)
            sdge_anim_player_destroy(m->players[i]);
        free(m->players);
    }
    if (m->static_palette) {
        for (int s = 0; s < m->gltf->skin_count; ++s)
            free(m->static_palette[s]);
        free(m->static_palette);
    }
    free(m->static_world);
    sdge_gltf_destroy(m->gltf);
    free(m);
}

SDGE_API int sdge_model_anim_count(const sdge_model *m) {
    return m ? m->gltf->anim_count : 0;
}

SDGE_API const char *sdge_model_anim_name(const sdge_model *m, int anim) {
    if (!m || anim < 0 || anim >= m->gltf->anim_count) return NULL;
    return m->gltf->anims[anim].name ? m->gltf->anims[anim].name : "";
}

SDGE_API void sdge_model_play(sdge_model *m, int anim_index, int loop) {
    if (!m || anim_index < 0 || anim_index >= m->player_count) return;
    if (!m->players[anim_index]) {
        m->players[anim_index] = sdge_anim_player_create(m->gltf, anim_index);
        if (!m->players[anim_index]) return;
    }
    /* don't restart the same clip every frame */
    if (m->active != m->players[anim_index])
        sdge_anim_player_reset(m->players[anim_index]);
    sdge_anim_player_set_looping(m->players[anim_index], loop);
    m->active = m->players[anim_index];
}

SDGE_API void sdge_model_stop(sdge_model *m) {
    if (m) m->active = NULL;
}

SDGE_API void sdge_model_advance(sdge_model *m, float dt) {
    if (m && m->active) sdge_anim_player_advance(m->active, dt);
}

SDGE_API void sdge_model_set_time(sdge_model *m, float seconds) {
    if (m && m->active) sdge_anim_player_set_time(m->active, seconds);
}

SDGE_API void sdge_model_draw(sdge_model *m, const sdg_mat4 model) {
    if (!m || !m->gltf) return;
    const sdge_gltf_model *g = m->gltf;

    sdg_mat4 base;
    if (model) m4_copy(base, model); else m4_identity(base);

    for (int i = 0; i < g->node_count; ++i) {
        const sdge_gltf_node *n = &g->nodes[i];
        if (n->mesh < 0 || n->mesh >= g->mesh_count || !m->prims[n->mesh])
            continue;

        sdg_mat4 world;
        if (m->active) {
            sdg_mat4 w;
            sdge_anim_player_get_world(m->active, i, w);
            m4_mul(world, base, w);
        } else if (m->static_world) {
            m4_mul(world, base, m->static_world[i]);
        } else {
            m4_copy(world, base);
        }

        const float *joints = NULL;
        int joint_count = 0;
        if (n->skin >= 0 && n->skin < g->skin_count) {
            if (m->active) {
                joints = sdge_anim_player_skin_palette(m->active,
                                                       &g->skins[n->skin]);
                joint_count = g->skins[n->skin].joint_count;
            } else if (m->static_palette && m->static_palette[n->skin]) {
                joints = m->static_palette[n->skin];
                joint_count = g->skins[n->skin].joint_count;
            }
        }

        sdge_gltf_mesh *mesh = &g->meshes[n->mesh];
        for (int p = 0; p < mesh->prim_count; ++p) {
            sdge_mesh *gpu = &m->prims[n->mesh][p];
            if (!gpu->vao) continue;

            float center[3], radius;
            sdge_mesh_world_sphere(gpu, world, center, &radius);
            if (!sdgr_sphere_visible(center, radius)) continue;

            const sdge_gltf_material *mat =
                (mesh->prims[p].material >= 0
                 && mesh->prims[p].material < g->material_count)
                    ? &g->materials[mesh->prims[p].material] : NULL;

            /* NEW: resolve baseColor texture */
            unsigned base_tex = 0;
            if (mat && mat->base_color_texture >= 0
                && mat->base_color_texture < g->texture_count
                && m->textures)
                base_tex = m->textures[mat->base_color_texture];

            if (mat)
                sdgr_draw_mesh(gpu, world, joints, joint_count,
                               mat->base_color, mat->metallic,
                               mat->roughness, mat->double_sided,
                               base_tex);
            else
                sdgr_draw_mesh(gpu, world, joints, joint_count,
                               NULL, 0.0f, 0.6f, 0, 0);
        }
    }
}