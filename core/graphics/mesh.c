/* mesh.c - Silver Diamond Game Engine :: GPU mesh implementation */
#include "mesh.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef SDGE_NO_GL

#include <glad/glad.h>

static float *m_to_f32(const sdge_accessor *a, int normalized_default) {
    if (a->count <= 0 || a->comps <= 0) return NULL;
    if (a->ctype == SDGE_GLTF_FLOAT && !a->normalized)
        return NULL;

    int n = a->count * a->comps;
    float *out = (float *)malloc(sizeof(float) * (size_t)n);
    if (!out) return NULL;

    int csz = (a->ctype == SDGE_GLTF_BYTE || a->ctype == SDGE_GLTF_UNSIGNED_BYTE) ? 1
            : (a->ctype == SDGE_GLTF_SHORT || a->ctype == SDGE_GLTF_UNSIGNED_SHORT) ? 2 : 4;
    int norm = a->normalized || (normalized_default && a->ctype != SDGE_GLTF_FLOAT);
    float maxv = (a->ctype == SDGE_GLTF_BYTE) ? 127.0f
              : (a->ctype == SDGE_GLTF_UNSIGNED_BYTE) ? 255.0f
              : (a->ctype == SDGE_GLTF_SHORT) ? 32767.0f
              : (a->ctype == SDGE_GLTF_UNSIGNED_SHORT) ? 65535.0f : 0.0f;

    for (int i = 0; i < n; ++i) {
        const unsigned char *p = a->data
            + (size_t)(i / a->comps) * (size_t)a->stride
            + (size_t)(i % a->comps) * (size_t)csz;
        float v = 0.0f;
        switch (a->ctype) {
        case SDGE_GLTF_BYTE:           v = (float)(signed char)p[0]; break;
        case SDGE_GLTF_UNSIGNED_BYTE:   v = (float)p[0]; break;
        case SDGE_GLTF_SHORT: {
            short x = (short)((unsigned short)p[0] | ((unsigned short)p[1] << 8));
            v = (float)x; break;
        }
        case SDGE_GLTF_UNSIGNED_SHORT:
            v = (float)((unsigned)p[0] | ((unsigned)p[1] << 8)); break;
        case SDGE_GLTF_UNSIGNED_INT: {
            unsigned int x = (unsigned int)p[0] | ((unsigned int)p[1] << 8)
                            | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
            v = (float)x; break;
        }
        case SDGE_GLTF_FLOAT: {
            memcpy(&v, p, 4); break;
        }
        default: v = 0.0f; break;
        }
        out[i] = (norm && maxv > 0.0f) ? v / maxv : v;
    }
    return out;
}

static void m_upload_attrib(unsigned *vbo, int location, int comps,
                             const sdge_accessor *a) {
    float *conv = m_to_f32(a, 0);
    const void *data = conv ? (const void *)conv : (const void *)a->data;
    int stride = conv ? 0 : a->stride;

    glGenBuffers(1, vbo);
    glBindBuffer(GL_ARRAY_BUFFER, *vbo);
    glBufferData(GL_ARRAY_BUFFER,
                  (long long)sizeof(float) * (long long)a->comps * a->count,
                  data, GL_STATIC_DRAW);
    glEnableVertexAttribArray((unsigned)location);
    glVertexAttribPointer((unsigned)location, comps, GL_FLOAT, GL_FALSE,
                          stride ? (unsigned)stride : (unsigned)(comps * sizeof(float)),
                          (void *)0);
    free(conv);
}

SDGE_API int sdge_mesh_upload(sdge_mesh *out, const sdge_primitive *prim,
                               const sdge_gltf_material *material) {
    if (!out || !prim) return -1;
    memset(out, 0, sizeof(*out));
    if (prim->positions.count <= 0 || prim->positions.comps != 3) return -1;

    out->double_sided = material ? material->double_sided : 0;

    glGenVertexArrays(1, &out->vao);
    glBindVertexArray(out->vao);

    m_upload_attrib(&out->vbo_pos, 0, 3, &prim->positions);
    if (prim->normals.count > 0 && prim->normals.comps == 3) {
        m_upload_attrib(&out->vbo_nrm, 1, 3, &prim->normals);
        out->has_normals = 1;
    }

    /* TEXCOORD_0 -> attribute 4 (aUV) */
    if (prim->texcoords.count > 0 && prim->texcoords.comps == 2
        && prim->texcoords.count == prim->positions.count) {
        m_upload_attrib(&out->vbo_tex, 4, 2, &prim->texcoords);
        out->has_texcoords = 1;
    }

    /* TEXCOORD_1 -> attribute 5 (aUV1) */
    if (prim->texcoords1.count > 0 && prim->texcoords1.comps == 2
        && prim->texcoords1.count == prim->positions.count) {
        m_upload_attrib(&out->vbo_tex1, 5, 2, &prim->texcoords1);
        out->has_texcoords2 = 1;
    }

    /* TANGENT -> attribute 6 (aTan, xyz + handedness) */
    if (prim->tangents.count > 0 && prim->tangents.comps == 4
        && prim->tangents.count == prim->positions.count) {
        m_upload_attrib(&out->vbo_tan, 6, 4, &prim->tangents);
        out->has_tangents = 1;
    }

    /* joints + weights interleaved as 8 floats (joint, weight) x4 */
    if (prim->joints.count > 0 && prim->weights.count > 0
        && prim->joints.comps == 4 && prim->weights.comps == 4
        && prim->joints.count == prim->positions.count) {
        float *jw = (float *)malloc(sizeof(float) * 8u * (size_t)prim->positions.count);
        if (!jw) { sdge_mesh_destroy(out); return -2; }
        float *joints  = m_to_f32(&prim->joints, 0);
        float *weights = m_to_f32(&prim->weights, 1);
        if (joints && weights) {
            for (int i = 0; i < prim->positions.count; ++i) {
                float wsum = weights[i*4+0] + weights[i*4+1]
                           + weights[i*4+2] + weights[i*4+3];
                float inv = wsum > 1e-6f ? 1.0f / wsum : 0.0f;
                for (int k = 0; k < 4; ++k) {
                    jw[i*8+k]   = joints[i*4+k];
                    jw[i*8+4+k] = weights[i*4+k] * inv;
                }
            }
            glGenBuffers(1, &out->vbo_skin);
            glBindBuffer(GL_ARRAY_BUFFER, out->vbo_skin);
            glBufferData(GL_ARRAY_BUFFER,
                         (long long)sizeof(float) * 8 * prim->positions.count,
                         jw, GL_STATIC_DRAW);
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 32, (void *)0);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 32, (void *)16);
            out->has_skin = 1;
        }
        free(joints);
        free(weights);
        free(jw);
    }

    out->vertex_count = prim->positions.count;
    if (prim->indices.count > 0) {
        const sdge_accessor *ia = &prim->indices;
        if (ia->ctype == SDGE_GLTF_UNSIGNED_SHORT || ia->ctype == SDGE_GLTF_UNSIGNED_INT) {
            glGenBuffers(1, &out->ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         (long long)ia->stride * ia->count, ia->data, GL_STATIC_DRAW);
            out->index_count = ia->count;
            out->index_type = (ia->ctype == SDGE_GLTF_UNSIGNED_SHORT) ? 3 : 4;
        } else {
            unsigned short *idx = (unsigned short *)malloc(
                sizeof(unsigned short) * (size_t)ia->count);
            if (!idx) { sdge_mesh_destroy(out); return -3; }
            int csz = (ia->ctype == SDGE_GLTF_BYTE || ia->ctype == SDGE_GLTF_UNSIGNED_BYTE) ? 1 : 2;
            for (int i = 0; i < ia->count; ++i) {
                const unsigned char *p = ia->data + (size_t)i * (size_t)ia->stride;
                if (csz == 1) idx[i] = (unsigned short)p[0];
                else idx[i] = (unsigned short)(p[0] | (p[1] << 8));
            }
            glGenBuffers(1, &out->ebo);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ebo);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                         (long long)sizeof(unsigned short) * ia->count, idx,
                         GL_STATIC_DRAW);
            free(idx);
            out->index_count = ia->count;
            out->index_type = 3;
        }
    }

    const sdge_accessor *pa = &prim->positions;
    float mn[3] = { 0,0,0 }, mx[3] = { 0,0,0 };
    int have = 0;
    if (pa->bounds_min[0] != 0.0f || pa->bounds_max[0] != 0.0f
        || pa->bounds_min[1] != 0.0f || pa->bounds_max[1] != 0.0f
        || pa->bounds_min[2] != 0.0f || pa->bounds_max[2] != 0.0f) {
        memcpy(mn, pa->bounds_min, sizeof(mn));
        memcpy(mx, pa->bounds_max, sizeof(mx));
        have = 1;
    } else {
        float *conv = m_to_f32(pa, 0);
        const float *pv = conv;
        if (!pv) {
            if (pa->ctype == SDGE_GLTF_FLOAT) {
                for (int i = 0; i < pa->count; ++i) {
                    const float *p = (const float *)(pa->data + (size_t)i * (size_t)pa->stride);
                    for (int k = 0; k < 3; ++k) {
                        if (!have || p[k] < mn[k]) mn[k] = p[k];
                        if (!have || p[k] > mx[k]) mx[k] = p[k];
                    }
                    have = 1;
                }
            }
        } else {
            for (int i = 0; i < pa->count * 3; ++i) {
                float v = conv[i];
                if (!have || v < mn[i % 3]) mn[i % 3] = v;
                if (!have || v > mx[i % 3]) mx[i % 3] = v;
                have = 1;
            }
        }
        free(conv);
    }
    out->bounds_center[0] = (mn[0] + mx[0]) * 0.5f;
    out->bounds_center[1] = (mn[1] + mx[1]) * 0.5f;
    out->bounds_center[2] = (mn[2] + mx[2]) * 0.5f;
    float r = 0.0f;
    for (int k = 0; k < 3; ++k) {
        float d = mx[k] - out->bounds_center[k];
        r += d * d;
    }
    out->bounds_radius = sqrtf(r);
    if (!have) out->bounds_radius = 1e9f;

    glBindVertexArray(0);
    return 0;
}

static int mesh_upload_raw(sdge_mesh *out, const float *verts, int vcount,
                           const unsigned short *idx, int icount) {
    memset(out, 0, sizeof(*out));
    glGenVertexArrays(1, &out->vao);
    glBindVertexArray(out->vao);

    glGenBuffers(1, &out->vbo_pos);
    glBindBuffer(GL_ARRAY_BUFFER, out->vbo_pos);
    glBufferData(GL_ARRAY_BUFFER,
                 (long long)sizeof(float) * 6 * (long long)vcount,
                 verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void *)12);

    glGenBuffers(1, &out->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, out->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 (long long)sizeof(unsigned short) * (long long)icount,
                 idx, GL_STATIC_DRAW);

    glBindVertexArray(0);
    out->vertex_count = vcount;
    out->index_count = icount;
    out->index_type = 3;
    out->has_normals = 1;
    return 0;
}

SDGE_API int sdge_mesh_create_box(sdge_mesh *out, float hw, float hh, float hd) {
    if (!out) return -1;
    static const float nrm[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    float verts[24 * 6];
    unsigned short idx[36];
    int vi = 0, ii = 0;
    for (int f = 0; f < 6; ++f) {
        int axis = f / 2;
        float sign = (f % 2 == 0) ? 1.0f : -1.0f;
        int u = (axis + 1) % 3, v = (axis + 2) % 3;
        float e[3] = { hw, hh, hd };
        float su = e[u], sv = e[v], sd = e[axis] * sign;
        float c[4][3];
        c[0][axis] = sd; c[0][u] = -su; c[0][v] = -sv;
        c[1][axis] = sd; c[1][u] =  su; c[1][v] = -sv;
        c[2][axis] = sd; c[2][u] =  su; c[2][v] =  sv;
        c[3][axis] = sd; c[3][u] = -su; c[3][v] =  sv;
        for (int k = 0; k < 4; ++k) {
            verts[vi * 6 + 0] = c[k][0];
            verts[vi * 6 + 1] = c[k][1];
            verts[vi * 6 + 2] = c[k][2];
            verts[vi * 6 + 3] = nrm[f][0];
            verts[vi * 6 + 4] = nrm[f][1];
            verts[vi * 6 + 5] = nrm[f][2];
            vi++;
        }
        unsigned short base = (unsigned short)(f * 4);
        if (sign > 0.0f) {
            idx[ii++] = base;     idx[ii++] = base + 1; idx[ii++] = base + 2;
            idx[ii++] = base;     idx[ii++] = base + 2; idx[ii++] = base + 3;
        } else {
            idx[ii++] = base;     idx[ii++] = base + 2; idx[ii++] = base + 1;
            idx[ii++] = base;     idx[ii++] = base + 3; idx[ii++] = base + 2;
        }
    }
    int rc = mesh_upload_raw(out, verts, 24, idx, 36);
    if (rc == 0)
        out->bounds_radius = sqrtf(hw * hw + hh * hh + hd * hd);
    return rc;
}

SDGE_API int sdge_mesh_create_sphere(sdge_mesh *out, float radius,
                                      int slices, int stacks) {
    if (!out) return -1;
    if (slices < 3) slices = 3;
    if (stacks < 2) stacks = 2;
    int vcount = (slices + 1) * (stacks + 1);
    int icount = slices * stacks * 6;
    float *verts = (float *)malloc(sizeof(float) * 6 * (size_t)vcount);
    unsigned short *idx = (unsigned short *)malloc(sizeof(unsigned short) *
                                                   (size_t)icount);
    if (!verts || !idx) { free(verts); free(idx); return -1; }

    int vi = 0;
    for (int s = 0; s <= stacks; ++s) {
        float phi = (float)M_PI * (float)s / (float)stacks;
        float y = cosf(phi), r = sinf(phi);
        for (int sl = 0; sl <= slices; ++sl) {
            float th = 2.0f * (float)M_PI * (float)sl / (float)slices;
            float x = r * cosf(th), z = r * sinf(th);
            verts[vi * 6 + 0] = x * radius;
            verts[vi * 6 + 1] = y * radius;
            verts[vi * 6 + 2] = z * radius;
            verts[vi * 6 + 3] = x;
            verts[vi * 6 + 4] = y;
            verts[vi * 6 + 5] = z;
            vi++;
        }
    }
    int ii = 0;
    for (int s = 0; s < stacks; ++s) {
        for (int sl = 0; sl < slices; ++sl) {
            unsigned short a = (unsigned short)(s * (slices + 1) + sl);
            unsigned short b = (unsigned short)(a + slices + 1);
            idx[ii++] = a;     idx[ii++] = a + 1; idx[ii++] = b;
            idx[ii++] = a + 1; idx[ii++] = b + 1; idx[ii++] = b;
        }
    }
    int rc = mesh_upload_raw(out, verts, vcount, idx, icount);
    free(verts);
    free(idx);
    if (rc == 0) out->bounds_radius = radius;
    return rc;
}

SDGE_API void sdge_mesh_destroy(sdge_mesh *m) {
    if (!m || !m->vao) return;
    glDeleteVertexArrays(1, &m->vao);
    if (m->vbo_pos)  glDeleteBuffers(1, &m->vbo_pos);
    if (m->vbo_nrm)  glDeleteBuffers(1, &m->vbo_nrm);
    if (m->vbo_tex)  glDeleteBuffers(1, &m->vbo_tex);
    if (m->vbo_tex1) glDeleteBuffers(1, &m->vbo_tex1);
    if (m->vbo_tan)  glDeleteBuffers(1, &m->vbo_tan);
    if (m->vbo_skin) glDeleteBuffers(1, &m->vbo_skin);
    if (m->ebo)      glDeleteBuffers(1, &m->ebo);
    memset(m, 0, sizeof(*m));
}

SDGE_API void sdge_mesh_world_sphere(const sdge_mesh *m, const sdg_mat4 world,
                                     float center_out[3], float *radius_out) {
    if (!m || !center_out || !radius_out) return;
    sdg_vec4 c, t;
    v4_set(c, m->bounds_center[0], m->bounds_center[1], m->bounds_center[2], 1.0f);
    m4_mul_vec4(t, world, c);
    center_out[0] = t[0] / (t[3] ? t[3] : 1.0f);
    center_out[1] = t[1] / (t[3] ? t[3] : 1.0f);
    center_out[2] = t[2] / (t[3] ? t[3] : 1.0f);
    float s = 0.0f;
    for (int col = 0; col < 3; ++col) {
        float l = sqrtf(world[col*4+0]*world[col*4+0]
                      + world[col*4+1]*world[col*4+1]
                      + world[col*4+2]*world[col*4+2]);
        if (l > s) s = l;
    }
    *radius_out = m->bounds_radius * s;
}

#else /* SDGE_NO_GL */

SDGE_API int sdge_mesh_upload(sdge_mesh *out, const sdge_primitive *prim,
                               const sdge_gltf_material *material) {
    (void)prim; (void)material;
    memset(out, 0, sizeof(*out));
    return -1;
}

SDGE_API void sdge_mesh_destroy(sdge_mesh *m) { (void)m; }

SDGE_API int sdge_mesh_create_box(sdge_mesh *out, float hw, float hh, float hd) {
    (void)hw; (void)hh; (void)hd;
    memset(out, 0, sizeof(*out));
    return -1;
}

SDGE_API int sdge_mesh_create_sphere(sdge_mesh *out, float radius,
                                      int slices, int stacks) {
    (void)radius; (void)slices; (void)stacks;
    memset(out, 0, sizeof(*out));
    return -1;
}

SDGE_API void sdge_mesh_world_sphere(const sdge_mesh *m, const sdg_mat4 world,
                                     float center_out[3], float *radius_out) {
    (void)m; (void)world;
    if (center_out) { center_out[0] = center_out[1] = center_out[2] = 0; }
    if (radius_out) *radius_out = 1e9f;
}

#endif /* SDGE_NO_GL */