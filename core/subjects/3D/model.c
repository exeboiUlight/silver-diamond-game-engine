/* model.c - Silver Diamond Game Engine :: 3D model subject implementation */
#include "model.h"
#include "../../graphics/renderer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct sdge_model {
    sdge_gltf_model *gltf;

    /* GPU meshes: prims[mesh_index] is an array with prim_count entries */
    sdge_mesh **prims;

    /* animation players, created lazily, parallel to gltf->anims */
    sdge_anim_player **players;
    int player_count;
    sdge_anim_player *active;          /* currently playing (NULL = bind pose) */

    /* static pose (bind): world matrices and per-skin palettes */
    sdg_mat4 *static_world;            /* [node_count] */
    float   **static_palette;          /* [skin_count] -> joint_count * 16 */
};

/* ------------------------------------------------------------ helpers */

/* Compute world matrices for all nodes from per-node local matrices.
 * A node's parent chain is resolved by iterating nodes in depth order
 * (parents always come first), so the pass is linear and allocation-free
 * after the depth sort. Runs once per load - not a per-frame cost. */
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
    /* order nodes by depth (insertion sort: node counts are small, and this
     * avoids pulling in qsort comparators for a once-per-load step) */
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

/* per-node local matrices for the bind pose = gltf node matrix itself */

/* ------------------------------------------------------------ create */
SDGE_API sdge_model *sdge_model_load(const char *path) {
    if (!path) return NULL;
    sdge_gltf_model *g = sdge_load_gltf(path);
    if (!g) return NULL;

    sdge_model *m = (sdge_model *)calloc(1, sizeof(*m));
    if (!m) { sdge_gltf_destroy(g); return NULL; }
    m->gltf = g;

    /* upload every primitive to the GPU */
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

    /* animation players (lazy) */
    m->player_count = g->anim_count;
    if (m->player_count > 0) {
        m->players = (sdge_anim_player **)calloc((size_t)m->player_count,
                                                 sizeof(*m->players));
        if (!m->players) { sdge_model_destroy(m); return NULL; }
    }

    /* static pose: world matrices from the node-local bind matrices, then
     * per-skin bind palettes (world * inverseBindMatrix) */
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

        /* per-skin bind palettes: world * inverseBindMatrix */
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

/* --------------------------------------------------------- animations */
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

/* --------------------------------------------------------------- draw */
SDGE_API void sdge_model_draw(sdge_model *m, const sdg_mat4 model) {
    if (!m || !m->gltf) return;
    const sdge_gltf_model *g = m->gltf;

    sdg_mat4 base;
    if (model) m4_copy(base, model); else m4_identity(base);

    /* node world matrix: player world (sampled) or bind world, then the
     * caller's transform on top. Iterating all nodes flat is correct:
     * each world matrix is self-contained (no recursion needed). */
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

        /* skin palette: animated pose, or the cached bind palette */
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

            /* frustum cull by world-space bounding sphere */
            float center[3], radius;
            sdge_mesh_world_sphere(gpu, world, center, &radius);
            if (!sdgr_sphere_visible(center, radius)) continue;

            const sdge_gltf_material *mat =
                (mesh->prims[p].material >= 0
                 && mesh->prims[p].material < g->material_count)
                    ? &g->materials[mesh->prims[p].material] : NULL;
            if (mat)
                sdgr_draw_mesh(gpu, world, joints, joint_count,
                               mat->base_color, mat->metallic,
                               mat->roughness, mat->double_sided);
            else
                sdgr_draw_mesh(gpu, world, joints, joint_count,
                               NULL, 0.0f, 0.6f, 0);
        }
    }
}
