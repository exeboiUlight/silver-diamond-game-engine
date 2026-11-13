/* model.c - Silver Diamond Game Engine :: 3D model subject implementation */
#include "model.h"
#include "../../graphics/renderer.h"
#include "../../assets/texture.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* texture usage flags - decide whether a texture is uploaded as sRGB
 * (color data) or as linear data (normal/metallic/occlusion maps) */
#define TU_COLOR     1
#define TU_EMISSIVE  2
#define TU_DATA      4

struct sdge_model {
    sdge_gltf_model *gltf;
    sdge_mesh **prims;

    /* GL textures, parallel to gltf->textures (0 = none) */
    unsigned *textures;

    /* GPU side material description, parallel to gltf->materials */
    sdgr_material *materials;
    sdgr_texture_set *texsets;

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

/* resolve a glTF image into RGBA8: external file, data-URI or GLB bufferView */
#ifndef SDGE_NO_GL
static int load_image_rgba(const sdge_gltf_model *g,
                           const sdge_gltf_image *im,
                           const char *gltf_path,
                           sdga_image *out) {
    memset(out, 0, sizeof(*out));

    /* embedded base64 payload */
    if (im->data && im->data_len > 0)
        return sdga_image_load_rgba_mem(im->data, im->data_len, out);

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

    return sdga_image_load_rgba_mem(bv->data, bv->byte_length, out);
}
#endif

/* which texture slots use which texture (decides sRGB vs linear upload) */
static void m_scan_usage(const sdge_gltf_model *g, unsigned char *usage) {
    for (int i = 0; i < g->material_count; ++i) {
        const sdge_gltf_material *mat = &g->materials[i];
        if (mat->base_color_tex.index >= 0
            && mat->base_color_tex.index < g->texture_count)
            usage[mat->base_color_tex.index] |= TU_COLOR;
        if (mat->emissive_tex.index >= 0
            && mat->emissive_tex.index < g->texture_count)
            usage[mat->emissive_tex.index] |= TU_EMISSIVE;
        if (mat->metallic_roughness_tex.index >= 0
            && mat->metallic_roughness_tex.index < g->texture_count)
            usage[mat->metallic_roughness_tex.index] |= TU_DATA;
        if (mat->normal_tex.index >= 0
            && mat->normal_tex.index < g->texture_count)
            usage[mat->normal_tex.index] |= TU_DATA;
        if (mat->occlusion_tex.index >= 0
            && mat->occlusion_tex.index < g->texture_count)
            usage[mat->occlusion_tex.index] |= TU_DATA;
    }
}

/* maps a glTF sampler to the renderer's texture description */
static void m_sampler_desc(const sdge_gltf_model *g, int texture_index,
                           sdgr_texture_desc *desc, int srgb) {
    memset(desc, 0, sizeof(*desc));
    desc->srgb = srgb;
    desc->wrap_s = SDGR_REPEAT;
    desc->wrap_t = SDGR_REPEAT;
    desc->min_filter = SDGR_LINEAR_MIPMAP_LINEAR;
    desc->mag_filter = SDGR_LINEAR;
    desc->mipmaps = 1;

    if (texture_index < 0 || texture_index >= g->texture_count) return;
    int si = g->textures[texture_index].sampler;
    if (si < 0 || si >= g->sampler_count) return;
    const sdge_gltf_sampler *sp = &g->samplers[si];

    if (sp->wrap_s) desc->wrap_s = sp->wrap_s;
    if (sp->wrap_t) desc->wrap_t = sp->wrap_t;
    if (sp->mag_filter) desc->mag_filter = sp->mag_filter;
    if (sp->min_filter) desc->min_filter = sp->min_filter;
    /* a min filter that needs mipmaps only works when we generate them */
    if (sp->min_filter == SDGR_NEAREST || sp->min_filter == SDGR_LINEAR)
        desc->mipmaps = 0;
}

/* copies one glTF texture reference into a renderer slot */
static void m_fill_slot(const sdge_gltf_model *g, const sdge_gltf_texinfo *ti,
                        int slot, sdgr_material *out) {
    if (ti->index < 0 || ti->index >= g->texture_count) return;
    out->uv_set[slot] = (ti->texcoord == 1) ? 1 : 0;
    out->uv_offset[slot][0] = ti->offset[0];
    out->uv_offset[slot][1] = ti->offset[1];
    out->uv_rot[slot] = ti->rotation;
    out->uv_scale[slot][0] = ti->scale_uv[0];
    out->uv_scale[slot][1] = ti->scale_uv[1];
}

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

    /* upload all textures (sRGB for color maps, linear for data maps) */
    if (g->texture_count > 0) {
        m->textures = (unsigned *)calloc((size_t)g->texture_count,
                                          sizeof(unsigned));
        if (!m->textures) { sdge_model_destroy(m); return NULL; }
#ifndef SDGE_NO_GL
        unsigned char *usage = (unsigned char *)calloc((size_t)g->texture_count,
                                                        1);
        m_scan_usage(g, usage);
        for (int i = 0; i < g->texture_count; ++i) {
            const sdge_gltf_texture *tx = &g->textures[i];
            if (tx->source < 0 || tx->source >= g->image_count) continue;
            sdga_image img;
            if (load_image_rgba(g, &g->images[tx->source], path, &img) != 0) {
                fprintf(stderr, "model: texture %d image load failed\n", i);
                continue;
            }
            int srgb = (usage[i] & (TU_COLOR | TU_EMISSIVE))
                       && !(usage[i] & TU_DATA);
            sdgr_texture_desc desc;
            m_sampler_desc(g, i, &desc, srgb);
            m->textures[i] = sdgr_upload_texture_ex(img.w, img.h, img.pixels,
                                                     &desc);
            sdga_image_free(&img);
        }
        free(usage);
#endif
    }

    /* build the GPU material description of every glTF material */
    if (g->material_count > 0) {
        m->materials = (sdgr_material *)calloc((size_t)g->material_count,
                                                sizeof(sdgr_material));
        m->texsets   = (sdgr_texture_set *)calloc((size_t)g->material_count,
                                                   sizeof(sdgr_texture_set));
        if (!m->materials || !m->texsets) { sdge_model_destroy(m); return NULL; }
        for (int i = 0; i < g->material_count; ++i) {
            const sdge_gltf_material *src = &g->materials[i];
            sdgr_material *dst = &m->materials[i];
            sdgr_texture_set *ts = &m->texsets[i];

            memcpy(dst->base_color, src->base_color, sizeof(dst->base_color));
            dst->metallic = src->metallic;
            dst->roughness = src->roughness;
            memcpy(dst->emissive, src->emissive, sizeof(dst->emissive));
            dst->normal_scale = src->normal_tex.scale;
            dst->occlusion_strength = src->occlusion_tex.scale;
            dst->alpha_cutoff = src->alpha_cutoff;
            dst->alpha_mode = (src->alpha_mode == SDGE_ALPHA_MASK)
                              ? SDGR_ALPHA_MASK
                              : (src->alpha_mode == SDGE_ALPHA_BLEND)
                                ? SDGR_ALPHA_BLEND : SDGR_ALPHA_OPAQUE;
            dst->double_sided = src->double_sided;
            for (int s = 0; s < SDGR_TEX_SLOTS; ++s) {
                dst->uv_set[s] = 0;
                dst->uv_scale[s][0] = 1.0f;
                dst->uv_scale[s][1] = 1.0f;
                dst->uv_offset[s][0] = 0.0f;
                dst->uv_offset[s][1] = 0.0f;
                dst->uv_rot[s] = 0.0f;
            }
            m_fill_slot(g, &src->base_color_tex, SDGR_SLOT_BASE, dst);
            m_fill_slot(g, &src->metallic_roughness_tex, SDGR_SLOT_MR, dst);
            m_fill_slot(g, &src->normal_tex, SDGR_SLOT_NORMAL, dst);
            m_fill_slot(g, &src->occlusion_tex, SDGR_SLOT_OCCLUSION, dst);
            m_fill_slot(g, &src->emissive_tex, SDGR_SLOT_EMISSIVE, dst);

            if (m->textures) {
                int bi = src->base_color_tex.index;
                int mi = src->metallic_roughness_tex.index;
                int ni = src->normal_tex.index;
                int oi = src->occlusion_tex.index;
                int ei = src->emissive_tex.index;
                if (bi >= 0 && bi < g->texture_count)
                    ts->base_color = m->textures[bi];
                if (mi >= 0 && mi < g->texture_count)
                    ts->metallic_roughness = m->textures[mi];
                if (ni >= 0 && ni < g->texture_count)
                    ts->normal = m->textures[ni];
                if (oi >= 0 && oi < g->texture_count)
                    ts->occlusion = m->textures[oi];
                if (ei >= 0 && ei < g->texture_count)
                    ts->emissive = m->textures[ei];
            }
        }
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
    /* free GL textures */
    if (m->textures) {
        for (int i = 0; i < m->gltf->texture_count; ++i)
            if (m->textures[i]) sdgr_free_texture(m->textures[i]);
        free(m->textures);
    }
    free(m->materials);
    free(m->texsets);
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

SDGE_API const sdge_gltf_model *sdge_model_gltf(const sdge_model *m) {
    return m ? m->gltf : NULL;
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

/* one drawable primitive collected for the current frame */
typedef struct {
    sdge_mesh *gpu;
    sdg_mat4 world;
    const float *joints;
    int joint_count;
    const sdgr_material *mat;
    const sdgr_texture_set *tex;
    int transparent;
    float dist;                 /* squared distance to the camera */
} m_item;

static int m_item_cmp_far_first(const void *a, const void *b) {
    const m_item *x = (const m_item *)a;
    const m_item *y = (const m_item *)b;
    if (x->dist < y->dist) return 1;        /* far away first */
    if (x->dist > y->dist) return -1;
    return 0;
}

SDGE_API void sdge_model_draw(sdge_model *m, const sdg_mat4 model) {
    if (!m || !m->gltf) return;
    const sdge_gltf_model *g = m->gltf;

    sdg_mat4 base;
    if (model) m4_copy(base, model); else m4_identity(base);

    float cam[3];
    sdgr_get_camera_pos(cam);

    /* ---- collect the visible primitives ---- */
    m_item *items = NULL;
    int count = 0, cap = 0;

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

            int mat_index = mesh->prims[p].material;
            const sdgr_material *mat = NULL;
            const sdgr_texture_set *tex = NULL;
            if (mat_index >= 0 && mat_index < g->material_count
                && m->materials) {
                mat = &m->materials[mat_index];
                tex = &m->texsets[mat_index];
            }

            if (count >= cap) {
                int new_cap = cap ? cap * 2 : 16;
                m_item *grown = (m_item *)realloc(items,
                                    sizeof(m_item) * (size_t)new_cap);
                if (!grown) { free(items); return; }
                items = grown;
                cap = new_cap;
            }
            m_item *it = &items[count++];
            it->gpu = gpu;
            m4_copy(it->world, world);
            it->joints = joints;
            it->joint_count = joint_count;
            it->mat = mat;
            it->tex = tex;
            it->transparent = (mat && mat->alpha_mode == SDGR_ALPHA_BLEND);
            float dx = center[0] - cam[0];
            float dy = center[1] - cam[1];
            float dz = center[2] - cam[2];
            it->dist = dx * dx + dy * dy + dz * dz;
        }
    }

    /* ---- opaque first, then the blended ones back to front ---- */
    for (int i = 0; i < count; ++i) {
        if (items[i].transparent) continue;
        sdgr_draw_mesh_ex(items[i].gpu, items[i].world, items[i].joints,
                          items[i].joint_count, items[i].mat, items[i].tex);
    }
    {
        int tc = 0;
        for (int i = 0; i < count; ++i)
            if (items[i].transparent) tc++;
        if (tc > 0) {
            m_item *tr = (m_item *)malloc(sizeof(m_item) * (size_t)tc);
            if (tr) {
                int w = 0;
                for (int i = 0; i < count; ++i)
                    if (items[i].transparent) tr[w++] = items[i];
                qsort(tr, (size_t)tc, sizeof(m_item), m_item_cmp_far_first);
                for (int i = 0; i < tc; ++i)
                    sdgr_draw_mesh_ex(tr[i].gpu, tr[i].world, tr[i].joints,
                                      tr[i].joint_count, tr[i].mat, tr[i].tex);
                free(tr);
            }
        }
    }
    free(items);
}