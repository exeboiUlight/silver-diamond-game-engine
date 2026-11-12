/* anim.c - Silver Diamond Game Engine :: animation player implementation
 *
 * Compile notes: single TU instantiating nothing - the code lives here
 * directly (the header stays a clean API). See anim.h for the design.
 */
#include "anim.h"

#include <stdlib.h>
#include <string.h>

struct sdge_anim_player {
    const sdge_gltf_model *m;
    const sdge_gltf_animation *a;

    int   node_count;
    int  *order;        /* topological: parents always before children */
    sdg_mat4 *local;    /* sampled local matrix per node */
    sdg_mat4 *world;    /* parent_chain * local per node */

    /* resolved channels per node (NULL when not animated) */
    const sdge_gltf_channel **ct;   /* translation */
    const sdge_gltf_channel **cr;   /* rotation */
    const sdge_gltf_channel **cs;   /* scale */

    float *palette;     /* skin matrices cache (joint_count_max * 16 floats) */
    int    palette_floats;
    const sdge_gltf_skin *palette_skin;   /* skin cached for */
    unsigned world_stamp;                 /* bumped on every world recompute */
    unsigned palette_stamp;                /* stamp the palette was built at */

    float time;
    float duration;
    int   looping;
    int   dirty;         /* time changed since the last local/world pass */
};

/* ------------------------------------------------------ key sampling */

/* find i so that times[i] <= t < times[i+1]; clamps at the ends.
 * returns the segment index and the normalized position inside it. */
static void g_find_segment(const float *times, int n, float t,
                            int *seg, float *u) {
    if (n == 1 || t <= times[0]) { *seg = 0; *u = 0.0f; return; }
    if (t >= times[n - 1])       { *seg = n - 2; *u = 1.0f; return; }
    int lo = 0, hi = n - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) >> 1;
        if (times[mid] <= t) lo = mid; else hi = mid;
    }
    *seg = lo;
    float span = times[lo + 1] - times[lo];
    *u = span > 0.0f ? (t - times[lo]) / span : 0.0f;
}

/* sample one channel into out[comps].
 * CUBICSPLINE value layout per key: [inTangent(comps), value(comps),
 * outTangent(comps)] - the spec formula with dt scaling. */
static void g_sample(const sdge_gltf_channel *ch, float t, float *out) {
    int comps = ch->comps;
    if (ch->key_count <= 0 || !ch->times || !ch->values) {
        memset(out, 0, sizeof(float) * (size_t)comps);
        return;
    }

    int cubic = (ch->interp == SDGE_ANIM_CUBIC);
    int vstep = cubic ? comps * 3 : comps;     /* values stride per key */

    int seg; float u;
    g_find_segment(ch->times, ch->key_count, t, &seg, &u);

    const float *k0 = ch->values + (size_t)seg * (size_t)vstep
                    + (size_t)(cubic ? comps : 0);
    if (ch->interp == SDGE_ANIM_STEP || ch->key_count == 1 || u <= 0.0f) {
        memcpy(out, k0, sizeof(float) * (size_t)comps);
        return;
    }
    const float *k1 = ch->values + (size_t)(seg + 1) * (size_t)vstep
                    + (size_t)(cubic ? comps : 0);

    if (ch->interp == SDGE_ANIM_CUBIC) {
        float dt = ch->times[seg + 1] - ch->times[seg];
        const float *m0 = k0 + comps;               /* outTangent of key 0 */
        const float *m1 = k1 - comps;               /* inTangent of key 1 */
        float u2 = u * u, u3 = u2 * u;
        float h00 =  2.0f * u3 - 3.0f * u2 + 1.0f;
        float h10 =      u3 - 2.0f * u2 + u;
        float h01 = -2.0f * u3 + 3.0f * u2;
        float h11 =      u3 -      u2;
        for (int i = 0; i < comps; ++i)
            out[i] = h00 * k0[i] + h10 * dt * m0[i]
                   + h01 * k1[i] + h11 * dt * m1[i];
    } else {
        /* LINEAR: quaternion channels take the shortest arc */
        if (ch->path == SDGE_ANIM_R && comps == 4) {
            sdg_quat qa, qb;
            memcpy(qa, k0, sizeof(qa));
            memcpy(qb, k1, sizeof(qb));
            q_slerp(out, qa, qb, u);
        } else {
            for (int i = 0; i < comps; ++i)
                out[i] = k0[i] + (k1[i] - k0[i]) * u;
        }
    }
}

/* ------------------------------------------------------ player core */

static void g_ensure_locals(sdge_anim_player *p) {
    if (!p->dirty) return;

    /* start from the static pose, then override animated T/R/S */
    for (int i = 0; i < p->node_count; ++i) {
        const sdge_gltf_node *n = &p->m->nodes[i];
        sdg_vec3 t, s; sdg_quat r;
        v3_copy(t, n->translation);
        q_copy(r, n->rotation);
        v3_copy(s, n->scale);

        if (p->ct && p->ct[i]) g_sample(p->ct[i], p->time, t);
        if (p->cr && p->cr[i]) {
            float rr[4];
            g_sample(p->cr[i], p->time, rr);
            q_norm(r, rr);
        }
        if (p->cs && p->cs[i]) g_sample(p->cs[i], p->time, s);

        /* static node with an explicit `matrix` (no TRS in the file, not
         * animated): use the stored matrix as-is; otherwise compose TRS */
        if (n->has_matrix && !p->ct[i] && !p->cr[i] && !p->cs[i])
            m4_copy(p->local[i], n->matrix);
        else
            m4_from_trs(p->local[i], t, r, s);
    }
    p->dirty = 0;
    p->world_stamp++;
}

static void g_ensure_world(sdge_anim_player *p) {
    g_ensure_locals(p);
    for (int k = 0; k < p->node_count; ++k) {
        int i = p->order[k];
        int parent = p->m->nodes[i].parent;
        if (parent >= 0 && parent != i)
            m4_mul(p->world[i], p->world[parent], p->local[i]);
        else
            m4_copy(p->world[i], p->local[i]);
    }
}

SDGE_API sdge_anim_player *sdge_anim_player_create(const sdge_gltf_model *m,
                                                    int anim_index) {
    if (!m || anim_index >= m->anim_count) return NULL;
    sdge_anim_player *p = (sdge_anim_player *)calloc(1, sizeof(*p));
    if (!p) return NULL;

    p->m = m;
    p->a = (anim_index >= 0) ? &m->anims[anim_index] : NULL;
    p->node_count = m->node_count;
    p->time = 0.0f;
    p->duration = p->a ? p->a->duration : 0.0f;
    p->looping = 1;
    p->dirty = 1;
    p->world_stamp = 1;
    p->palette_stamp = 0;
    p->palette_skin = NULL;

    int n = p->node_count;
    if (n <= 0) return p;

    p->order  = (int *)malloc(sizeof(int) * (size_t)n);
    p->local  = (sdg_mat4 *)malloc(sizeof(sdg_mat4) * (size_t)n);
    p->world  = (sdg_mat4 *)malloc(sizeof(sdg_mat4) * (size_t)n);
    p->ct = (const sdge_gltf_channel **)calloc((size_t)n, sizeof(*p->ct));
    p->cr = (const sdge_gltf_channel **)calloc((size_t)n, sizeof(*p->cr));
    p->cs = (const sdge_gltf_channel **)calloc((size_t)n, sizeof(*p->cs));
    if (!p->order || !p->local || !p->world || !p->ct || !p->cr || !p->cs) {
        sdge_anim_player_destroy(p);
        return NULL;
    }

    /* resolve animation channels to their target nodes once */
    if (p->a)
        for (int c = 0; c < p->a->channel_count; ++c) {
            const sdge_gltf_channel *ch = &p->a->channels[c];
            if (ch->node < 0 || ch->node >= n) continue;
            if      (ch->path == SDGE_ANIM_T) p->ct[ch->node] = ch;
            else if (ch->path == SDGE_ANIM_R) p->cr[ch->node] = ch;
            else if (ch->path == SDGE_ANIM_S) p->cs[ch->node] = ch;
            /* SDGE_ANIM_W (morph weights): not supported yet */
        }

    /* topological order: iterative DFS from scene roots; unreached nodes
     * (orphans/cycles) are appended so their world pass is still defined */
    {
        char *seen = (char *)calloc((size_t)n, 1);
        int *stack = (int *)malloc(sizeof(int) * (size_t)n);
        if (!seen || !stack) { free(seen); free(stack);
                               sdge_anim_player_destroy(p); return NULL; }
        int top = 0, w = 0;
        for (int r = 0; r < m->root_count && m->roots; ++r) {
            int root = m->roots[r];
            if (root < 0 || root >= n || seen[root]) continue;
            seen[root] = 1;
            stack[top++] = root;
        }
        /* any unvisited parentless node is also a root */
        for (int i = 0; i < n; ++i)
            if (!seen[i] && m->nodes[i].parent < 0) { seen[i] = 1; stack[top++] = i; }
        while (top > 0) {
            int i = stack[--top];
            p->order[w++] = i;
            const sdge_gltf_node *node = &m->nodes[i];
            for (int c = node->child_count - 1; c >= 0; --c) {
                int ch = node->children[c];
                if (ch >= 0 && ch < n && !seen[ch]) { seen[ch] = 1; stack[top++] = ch; }
            }
        }
        for (int i = 0; i < n; ++i)                 /* leftovers */
            if (!seen[i]) p->order[w++] = i;
        free(seen);
        free(stack);
    }

    /* palette cache sized for the largest skin */
    int max_joints = 0;
    for (int s = 0; s < m->skin_count; ++s)
        if (m->skins[s].joint_count > max_joints) max_joints = m->skins[s].joint_count;
    if (max_joints > 0) {
        p->palette_floats = max_joints * 16;
        p->palette = (float *)malloc(sizeof(float) * (size_t)p->palette_floats);
        if (!p->palette) { sdge_anim_player_destroy(p); return NULL; }
    }
    return p;
}

SDGE_API void sdge_anim_player_destroy(sdge_anim_player *p) {
    if (!p) return;
    free(p->order);
    free(p->local);
    free(p->world);
    free(p->ct); free(p->cr); free(p->cs);
    free(p->palette);
    free(p);
}

SDGE_API void sdge_anim_player_reset(sdge_anim_player *p) {
    if (!p) return;
    p->time = 0.0f;
    p->dirty = 1;
}

SDGE_API void sdge_anim_player_advance(sdge_anim_player *p, float dt) {
    if (!p || p->duration <= 0.0f) return;
    p->time += dt;
    if (p->looping) {
        while (p->time >= p->duration) p->time -= p->duration;
        if (p->time < 0.0f) p->time += p->duration;
    } else if (p->time > p->duration) {
        p->time = p->duration;
    }
    p->dirty = 1;
}

SDGE_API void sdge_anim_player_set_time(sdge_anim_player *p, float seconds) {
    if (!p) return;
    if (seconds < 0.0f) seconds = 0.0f;
    if (p->duration > 0.0f && seconds > p->duration) seconds = p->duration;
    p->time = seconds;
    p->dirty = 1;
}

SDGE_API float sdge_anim_player_get_time(const sdge_anim_player *p) {
    return p ? p->time : 0.0f;
}

SDGE_API float sdge_anim_player_get_duration(const sdge_anim_player *p) {
    return p ? p->duration : 0.0f;
}

SDGE_API void sdge_anim_player_set_looping(sdge_anim_player *p, int loop) {
    if (p) p->looping = loop ? 1 : 0;
}

SDGE_API int sdge_anim_player_is_looping(const sdge_anim_player *p) {
    return p ? p->looping : 0;
}

SDGE_API void sdge_anim_player_get_local(const sdge_anim_player *p,
                                         int node, sdg_mat4 out) {
    if (!p || !out || node < 0 || node >= p->node_count) { if (out) m4_identity(out); return; }
    g_ensure_locals((sdge_anim_player *)p);
    m4_copy(out, p->local[node]);
}

SDGE_API void sdge_anim_player_get_world(const sdge_anim_player *p,
                                          int node, sdg_mat4 out) {
    if (!p || !out || node < 0 || node >= p->node_count) { if (out) m4_identity(out); return; }
    g_ensure_world((sdge_anim_player *)p);
    m4_copy(out, p->world[node]);
}

SDGE_API const float *sdge_anim_player_skin_palette(const sdge_anim_player *p,
                                                    const sdge_gltf_skin *skin) {
    if (!p || !skin || !p->palette) return NULL;
    if (skin->joint_count <= 0
        || skin->joint_count * 16 > p->palette_floats)
        return NULL;
    if (p->palette_skin == skin && p->palette_stamp == p->world_stamp)
        return p->palette;                      /* reuse the cached pass */

    g_ensure_world((sdge_anim_player *)p);
    for (int j = 0; j < skin->joint_count; ++j) {
        int node = skin->joints[j];
        sdg_mat4 world, ibm, pal;
        if (node >= 0 && node < p->node_count) m4_copy(world, p->world[node]);
        else                                   m4_identity(world);
        if (skin->inverse_bind) {
            memcpy(ibm, skin->inverse_bind + (size_t)j * 16, sizeof(ibm));
            m4_mul(pal, world, ibm);
        } else {
            m4_copy(pal, world);
        }
        memcpy(p->palette + (size_t)j * 16, pal, sizeof(pal));
    }
    ((sdge_anim_player *)p)->palette_skin = skin;
    ((sdge_anim_player *)p)->palette_stamp = p->world_stamp;
    return p->palette;
}
