/* gltf.h - Silver Diamond Game Engine :: glTF 2.0 asset loader
 *
 * Loads .glb (binary glTF, recommended) and .gltf (+ external .bin).
 * Output is a flat, render-ready data model in ONE arena:
 *   - vertex/index data stays ZERO-COPY: accessors point straight into the
 *     binary chunk (GPU upload can read them directly);
 *   - animations/skin data are converted to float32 once at load;
 *   - one free call releases the whole model (arena blocks).
 *
 * Supported: nodes (TRS + matrix), meshes/primitives, skins (inverse bind
 * matrices), animations (LINEAR/STEP/CUBICSPLINE on T/R/S), full PBR
 * materials (baseColor / metallicRoughness / normal / occlusion / emissive
 * textures + factors, alphaMode, doubleSided, KHR_texture_transform),
 * images/textures/samplers (external files, data-URIs, GLB bufferViews),
 * TEXCOORD_0/1 and TANGENT vertex attributes.
 * Not supported (v1): morph targets (WEIGHTS), sparse accessors, Draco.
 */
#ifndef SDGE_GLTF_H
#define SDGE_GLTF_H

#include "../sdge_def.h"
#include "../math/math3d.h"
#include "assets.h"

#ifdef __cplusplus
extern "C" {
#endif

/* glTF 2.0 component types */
#define SDGE_GLTF_BYTE           5120
#define SDGE_GLTF_UNSIGNED_BYTE  5121
#define SDGE_GLTF_SHORT          5122
#define SDGE_GLTF_UNSIGNED_SHORT 5123
#define SDGE_GLTF_UNSIGNED_INT   5125
#define SDGE_GLTF_FLOAT          5126

/* resolved accessor: a typed view into the model's binary data */
typedef struct {
    const unsigned char *data;
    int count;          /* element count */
    int comps;          /* components per element (SCALAR=1 .. MAT4=16) */
    int ctype;          /* one of SDGE_GLTF_* */
    int stride;         /* bytes between elements (0 = tightly packed) */
    int normalized;
    float bounds_min[3];
    float bounds_max[3];
} sdge_accessor;

/* NEW: a raw slice of the binary chunk (used by embedded images) */
typedef struct {
    const unsigned char *data;
    int byte_length;
    int byte_offset;
    int byte_stride;    /* 0 = tightly packed */
} sdge_gltf_buffer_view;

typedef struct {
    sdge_accessor positions;
    sdge_accessor normals;
    sdge_accessor texcoords;   /* TEXCOORD_0 */
    sdge_accessor texcoords1;  /* TEXCOORD_1 */
    sdge_accessor tangents;    /* TANGENT (VEC4: xyz + handedness) */
    sdge_accessor colors;      /* COLOR_0 */
    sdge_accessor joints;      /* JOINTS_0 (VEC4) */
    sdge_accessor weights;     /* WEIGHTS_0 (VEC4) */
    sdge_accessor indices;     /* SCALAR */
    int material;              /* index into materials, -1 = default */
    int mode;                  /* 4 = triangles (only mode supported) */
    int has_skin;
} sdge_primitive;

typedef struct {
    char *name;
    sdge_primitive *prims;
    int prim_count;
} sdge_gltf_mesh;

typedef struct {
    int parent;                /* -1 for roots */
    int *children;
    int child_count;
    int mesh;                   /* -1 = no mesh */
    int skin;                   /* -1 = no skin */
    int has_matrix;             /* 1 when 'matrix' was given */
    sdg_mat4 matrix;            /* valid when has_matrix */
    sdg_vec3 translation;
    sdg_quat rotation;
    sdg_vec3 scale;
    char *name;
} sdge_gltf_node;

typedef struct {
    int *joints;                /* node indices, joint_count entries */
    int joint_count;
    float *inverse_bind;       /* joint_count * 16 floats */
    int skeleton_root;          /* node index or -1 */
} sdge_gltf_skin;

typedef enum { SDGE_ANIM_LINEAR = 0, SDGE_ANIM_STEP, SDGE_ANIM_CUBIC } sdge_interp;
typedef enum { SDGE_ANIM_T = 0, SDGE_ANIM_R, SDGE_ANIM_S, SDGE_ANIM_W } sdge_anim_path;

typedef struct {
    int node;                   /* animated node index */
    sdge_anim_path path;
    sdge_interp interp;
    int key_count;
    float *times;               /* key_count floats (seconds) */
    float *values;              /* key_count * comps floats */
    int comps;                  /* 3 (T/S) or 4 (R) */
} sdge_gltf_channel;

typedef struct {
    char *name;
    int channel_count;
    sdge_gltf_channel *channels;
    float duration;             /* seconds (max key time) */
} sdge_gltf_animation;

/* NEW: image / texture / sampler */
typedef struct {
    char *name;
    char *uri;                  /* external file; NULL for GLB images */
    int   uri_len;              /* length of uri (0 when none) */
    int   buffer_view;          /* glTF image.bufferView, -1 if none */
    const unsigned char *data;  /* decoded "data:" URI payload (arena owned) */
    int   data_len;
    char *mime_type;            /* "image/png", "image/jpeg", NULL */
} sdge_gltf_image;

typedef struct {
    int source;                 /* index into images, -1 if none */
    int sampler;                /* index into samplers, -1 if default */
} sdge_gltf_texture;

typedef struct {
    int mag_filter;             /* GL constant, 0 = default */
    int min_filter;
    int wrap_s, wrap_t;
} sdge_gltf_sampler;

/* alpha handling of a material */
#define SDGE_ALPHA_OPAQUE 0      /* fully opaque */
#define SDGE_ALPHA_MASK   1      /* alpha test against alpha_cutoff */
#define SDGE_ALPHA_BLEND  2      /* alpha blending */

/* a reference to one texture slot of a material (glTF "textureInfo") */
typedef struct {
    int index;                  /* index into model->textures, -1 = none */
    int texcoord;               /* TEXCOORD_n set used by this slot */
    float scale;                /* normal scale / occlusion strength */
    int has_transform;          /* KHR_texture_transform present */
    float offset[2];            /* transform offset */
    float rotation;             /* transform rotation (radians) */
    float scale_uv[2];          /* transform scale */
} sdge_gltf_texinfo;

typedef struct {
    char *name;
    float base_color[4];
    float metallic;
    float roughness;
    float emissive[3];
    int double_sided;
    int alpha_mode;             /* SDGE_ALPHA_* */
    float alpha_cutoff;

    sdge_gltf_texinfo base_color_tex;
    sdge_gltf_texinfo metallic_roughness_tex;
    sdge_gltf_texinfo normal_tex;
    sdge_gltf_texinfo occlusion_tex;
    sdge_gltf_texinfo emissive_tex;
} sdge_gltf_material;

typedef struct {
    sdge_gltf_node *nodes;     int node_count;
    int *roots;                int root_count;   /* scene root node indices */
    sdge_gltf_mesh *meshes;    int mesh_count;
    sdge_gltf_skin *skins;     int skin_count;
    sdge_gltf_animation *anims; int anim_count;
    sdge_gltf_material *materials; int material_count;

    /* NEW */
    sdge_gltf_buffer_view *buffer_views; int buffer_view_count;
    sdge_gltf_image   *images;     int image_count;
    sdge_gltf_texture *textures;   int texture_count;
    sdge_gltf_sampler *samplers;   int sampler_count;

    sdga_arena arena;          /* owns all memory above */
} sdge_gltf_model;

/* returns NULL on failure (message goes to the engine error callback) */
SDGE_API sdge_gltf_model *sdge_load_gltf(const char *path);
SDGE_API void sdge_gltf_destroy(sdge_gltf_model *m);

#ifdef __cplusplus
}
#endif

/* ============================================================ IMPLEMENTATION
 * Compile in exactly ONE translation unit (the engine DLL or a static test):
 *     #define SDGE_GLTF_IMPLEMENTATION
 *     #include "gltf.h"
 * The implementation calls sdga_* from assets.h: link with the assets
 * implementation (assets.c, or SDGE_ASSETS_IMPLEMENTATION in the same TU).
 */
#ifdef SDGE_GLTF_IMPLEMENTATION

#include "json.h"
#include <stdio.h>

/* little-endian readers (static: private to this TU) */
static unsigned int g_u32(const unsigned char *p) {
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8)
         | ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static int g_ctype_size(int ct) {
    switch (ct) {
    case SDGE_GLTF_BYTE: case SDGE_GLTF_UNSIGNED_BYTE: return 1;
    case SDGE_GLTF_SHORT: case SDGE_GLTF_UNSIGNED_SHORT: return 2;
    case SDGE_GLTF_UNSIGNED_INT: case SDGE_GLTF_FLOAT: return 4;
    default: return 0;
    }
}

static float g_comp_f32(const unsigned char *p, int ct) {
    switch (ct) {
    case SDGE_GLTF_BYTE:           return (float)(signed char)p[0];
    case SDGE_GLTF_UNSIGNED_BYTE:   return (float)p[0];
    case SDGE_GLTF_SHORT: { short v = (short)((unsigned short)p[0]
                                    | ((unsigned short)p[1] << 8)); return (float)v; }
    case SDGE_GLTF_UNSIGNED_SHORT:  return (float)((unsigned)p[0] | ((unsigned)p[1] << 8));
    case SDGE_GLTF_UNSIGNED_INT:    return (float)g_u32(p);
    case SDGE_GLTF_FLOAT: { float f; memcpy(&f, p, 4); return f; }
    default: return 0.0f;
    }
}

static int g_comps_of_type(const char *t) {
    if (!t) return 0;
    if (!strcmp(t, "SCALAR")) return 1;
    if (!strcmp(t, "VEC2"))   return 2;
    if (!strcmp(t, "VEC3"))   return 3;
    if (!strcmp(t, "VEC4"))   return 4;
    if (!strcmp(t, "MAT4"))   return 16;
    return 0;
}

typedef struct {
    const unsigned char *bin;    /* buffer 0 data (arena-owned) */
    size_t bin_len;
} g_buffers;

/* parse one accessor token into a resolved view; returns 0 on success */
static int g_parse_accessor(sdj_doc *d, int acc, const g_buffers *bufs,
                            sdge_accessor *out) {
    memset(out, 0, sizeof(*out));
    if (acc < 0) return -1;

    int view_tok = sdj_obj_get(d, acc, "bufferView");
    int count    = (int)sdj_num(d, sdj_obj_get(d, acc, "count"));
    int ctype    = (int)sdj_num(d, sdj_obj_get(d, acc, "componentType"));
    int off      = (int)sdj_num(d, sdj_obj_get(d, acc, "byteOffset"));
    char tbuf[16];
    tbuf[0] = '\0';
    int type_tok = sdj_obj_get(d, acc, "type");
    if (type_tok >= 0)
        sdj_str(d, type_tok, tbuf, (int)sizeof(tbuf));
    int comps = g_comps_of_type(tbuf);
    int normalized = 0;
    {
        int n = sdj_obj_get(d, acc, "normalized");
        if (n >= 0) {
            const sdj_token *k = &d->tok[n];
            normalized = (d->src[k->start] == 't');
        }
    }
    if (count <= 0 || comps == 0 || g_ctype_size(ctype) == 0)
        return -2;

    size_t view_off = 0;
    int stride = 0;
    const unsigned char *base = NULL;
    if (view_tok >= 0) {
        int vi = (int)sdj_num(d, view_tok);
        int views = sdj_obj_get(d, 0, "bufferViews");
        int vt = sdj_arr_at(d, views, vi);
        if (vt < 0) return -3;
        view_off = (size_t)sdj_num(d, sdj_obj_get(d, vt, "byteOffset"));
        stride   = (int)sdj_num(d, sdj_obj_get(d, vt, "byteStride"));
        int buffer_idx = (int)sdj_num(d, sdj_obj_get(d, vt, "buffer"));
        if (buffer_idx != 0) return -4;
        base = bufs->bin;
        if (!base) return -5;
    } else {
        return -6;                                     /* sparse unsupported */
    }

    int csz = g_ctype_size(ctype);
    if (stride == 0) stride = comps * csz;
    const unsigned char *data = base + view_off + (size_t)off;
    if ((size_t)(data - base) > bufs->bin_len) return -7;
    if (count > 0
        && (size_t)(data - base) + (size_t)(count - 1) * (size_t)stride
               + (size_t)(comps * csz) > bufs->bin_len)
        return -8;

    out->data = data;
    out->count = count;
    out->comps = comps;
    out->ctype = ctype;
    out->stride = stride;
    out->normalized = normalized;

    int mn = sdj_obj_get(d, acc, "min");
    int mx = sdj_obj_get(d, acc, "max");
    for (int i = 0; i < 3; ++i) {
        out->bounds_min[i] = (float)sdj_num(d, sdj_arr_at(d, mn, i));
        out->bounds_max[i] = (float)sdj_num(d, sdj_arr_at(d, mx, i));
    }
    return 0;
}

static float *g_accessor_to_f32(sdge_gltf_model *m, const sdge_accessor *a) {
    int n = a->count * a->comps;
    float *out = (float *)sdga_alloc(&m->arena, sizeof(float) * (size_t)n);
    if (!out) return NULL;
    int csz = g_ctype_size(a->ctype);
    for (int i = 0; i < n; ++i) {
        const unsigned char *p = a->data + (size_t)(i / a->comps) * (size_t)a->stride
                               + (size_t)(i % a->comps) * (size_t)csz;
        out[i] = g_comp_f32(p, a->ctype);
    }
    return out;
}

/* raw (not copied, not NUL-terminated) string value of a token */
static const char *g_str_raw(const sdj_doc *d, int t, int *len_out) {
    if (t < 0 || t >= d->count || d->tok[t].type != SDJ_STRING) return NULL;
    if (len_out) *len_out = d->tok[t].end - d->tok[t].start;
    return d->src + d->tok[t].start;
}

static char *g_str(sdge_gltf_model *m, const sdj_doc *d, int t) {
    char tmp[256];
    int n = sdj_str(d, t, tmp, (int)sizeof(tmp));
    if (n < 0) return NULL;
    return sdga_strdup(&m->arena, tmp);
}

/* duplicates a string token of ANY length into the arena (data-URIs can be
 * megabytes long, so the 256-byte helper above must not be used for them) */
static char *g_strdup_raw(sdge_gltf_model *m, const sdj_doc *d, int t,
                          int *len_out) {
    int len = 0;
    const char *raw = g_str_raw(d, t, &len);
    if (!raw) return NULL;
    char *out = (char *)sdga_alloc(&m->arena, (size_t)len + 1u);
    if (!out) return NULL;
    memcpy(out, raw, (size_t)len);
    out[len] = '\0';
    if (len_out) *len_out = len;
    return out;
}

/* ---------------------------------------------------------------- base64
 * glTF allows "data:<mime>;base64,<payload>" URIs for buffers and images.
 * The decoder is allocation-light: it returns a malloc'ed buffer the caller
 * frees (or NULL on malformed input). */
static int g_b64_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static unsigned char *g_base64_decode(const char *src, size_t srclen,
                                      size_t *out_len) {
    unsigned char *out = (unsigned char *)malloc(srclen / 4u * 3u + 4u);
    if (!out) return NULL;
    size_t o = 0;
    int acc = 0, bits = 0;
    for (size_t i = 0; i < srclen; ++i) {
        char c = src[i];
        if (c == '=' ) break;
        int v = g_b64_value(c);
        if (v < 0) continue;                 /* skip whitespace/newlines */
        acc = (acc << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = (unsigned char)((acc >> bits) & 0xFF);
        }
    }
    *out_len = o;
    return out;
}

/* decodes "data:...;base64,<payload>" - returns NULL when not a data URI */
static unsigned char *g_data_uri_decode(const char *uri, size_t urilen,
                                        size_t *out_len) {
    if (!uri || urilen < 5 || strncmp(uri, "data:", 5) != 0) return NULL;
    const char *comma = (const char *)memchr(uri, ',', urilen);
    if (!comma) return NULL;
    size_t headlen = (size_t)(comma - uri) - 5;
    const char *head = uri + 5;
    int is_b64 = (headlen >= 7 && strncmp(head + headlen - 7, ";base64", 7) == 0);
    if (!is_b64) return NULL;                /* plain (percent) data URIs */
    const char *payload = comma + 1;
    size_t plen = urilen - (size_t)(payload - uri);
    return g_base64_decode(payload, plen, out_len);
}

/* ------------------------------------------------------------- textureInfo
 * parses {"index":n, "texCoord":k, "scale":s, "extensions":{...}} */
static void g_parse_texinfo(sdj_doc *d, int tok, int texture_count,
                            sdge_gltf_texinfo *out) {
    memset(out, 0, sizeof(*out));
    out->index = -1;
    out->texcoord = 0;
    out->scale = 1.0f;
    out->offset[0] = out->offset[1] = 0.0f;
    out->scale_uv[0] = out->scale_uv[1] = 1.0f;
    if (tok < 0) return;

    int idx = sdj_obj_get(d, tok, "index");
    if (idx >= 0) {
        int v = (int)sdj_num(d, idx);
        out->index = (v >= 0 && v < texture_count) ? v : -1;
    }
    int tc = sdj_obj_get(d, tok, "texCoord");
    if (tc >= 0) {
        int v = (int)sdj_num(d, tc);
        out->texcoord = (v >= 0 && v <= 7) ? v : 0;
    }
    int sc = sdj_obj_get(d, tok, "scale");
    if (sc >= 0) out->scale = (float)sdj_num(d, sc);

    /* KHR_texture_transform: offset / rotation / scale */
    int ext = sdj_obj_get(d, tok, "extensions");
    int ktt = sdj_obj_get(d, ext, "KHR_texture_transform");
    if (ktt >= 0) {
        out->has_transform = 1;
        int o = sdj_obj_get(d, ktt, "offset");
        if (sdj_len(d, o) == 2) {
            out->offset[0] = (float)sdj_num(d, sdj_arr_at(d, o, 0));
            out->offset[1] = (float)sdj_num(d, sdj_arr_at(d, o, 1));
        }
        int r = sdj_obj_get(d, ktt, "rotation");
        if (r >= 0) out->rotation = (float)sdj_num(d, r);
        int s = sdj_obj_get(d, ktt, "scale");
        if (sdj_len(d, s) == 2) {
            out->scale_uv[0] = (float)sdj_num(d, sdj_arr_at(d, s, 0));
            out->scale_uv[1] = (float)sdj_num(d, sdj_arr_at(d, s, 1));
        }
    }
}

SDGE_API void sdge_gltf_destroy(sdge_gltf_model *m) {
    if (!m) return;
    sdga_arena_destroy(&m->arena);
    free(m);
}

static int g_load_buffer0(sdj_doc *d, const char *gltf_path,
                          sdge_gltf_model *m, g_buffers *bufs) {
    int bufs_tok = sdj_obj_get(d, 0, "buffers");
    int b0 = sdj_arr_at(d, bufs_tok, 0);
    if (b0 < 0) return -1;
    int uri = sdj_obj_get(d, b0, "uri");
    if (uri < 0) return -2;

    char ubuf[512];
    int ulen = 0;
    const char *raw = g_str_raw(d, uri, &ulen);
    if (!raw) return -3;

    /* "data:...;base64,..." - decode straight into the arena */
    if (ulen > 5 && !strncmp(raw, "data:", 5)) {
        size_t len = 0;
        unsigned char *dec = g_data_uri_decode(raw, (size_t)ulen, &len);
        if (!dec || len == 0) { free(dec); return -4; }
        unsigned char *copy = (unsigned char *)sdga_alloc(&m->arena, len);
        if (!copy) { free(dec); return -7; }
        memcpy(copy, dec, len);
        free(dec);
        bufs->bin = copy;
        bufs->bin_len = len;
        return 0;
    }

    if (ulen > (int)sizeof(ubuf) - 1) return -3;
    memcpy(ubuf, raw, (size_t)ulen);
    ubuf[ulen] = '\0';

    char path[1024];
    const char *slash = strrchr(gltf_path, '/');
    const char *bslash = strrchr(gltf_path, '\\');
    const char *sep = (slash && (!bslash || slash > bslash)) ? slash : bslash;
    if (sep) {
        size_t dir = (size_t)(sep - gltf_path) + 1;
        if (dir + strlen(ubuf) + 1 > sizeof(path)) return -5;
        memcpy(path, gltf_path, dir);
        strcpy(path + dir, ubuf);
    } else {
        if (strlen(ubuf) + 1 > sizeof(path)) return -5;
        strcpy(path, ubuf);
    }

    unsigned char *data = NULL;
    size_t size = 0;
    if (sdga_read_file(path, &data, &size) != 0) return -6;
    unsigned char *arena_copy = (unsigned char *)sdga_alloc(&m->arena, size);
    if (!arena_copy) { free(data); return -7; }
    memcpy(arena_copy, data, size);
    free(data);
    bufs->bin = arena_copy;
    bufs->bin_len = size;
    return 0;
}

SDGE_API sdge_gltf_model *sdge_load_gltf(const char *path) {
    unsigned char *file = NULL;
    size_t file_size = 0;
    if (sdga_read_file(path, &file, &file_size) != 0 || !file) {
        fprintf(stderr, "gltf: cannot open '%s'\n", path);
        return NULL;
    }

    sdge_gltf_model *m = (sdge_gltf_model *)calloc(1, sizeof(*m));
    if (!m) { free(file); return NULL; }
    sdga_arena_init(&m->arena, 65536);

    const char *json = NULL;
    g_buffers bufs;
    memset(&bufs, 0, sizeof(bufs));

    int is_glb = (file_size >= 12 && g_u32(file) == 0x46546C67u);
    if (is_glb) {
        unsigned int version = g_u32(file + 4);
        unsigned int total   = g_u32(file + 8);
        if (version != 2 || total > file_size) goto fail;
        size_t pos = 12;
        int have_json = 0;
        while (pos + 8 <= file_size) {
            unsigned int clen = g_u32(file + pos);
            unsigned int ctype = g_u32(file + pos + 4);
            const unsigned char *body = file + pos + 8;
            if (pos + 8 + clen > file_size) break;
            if (ctype == 0x4E4F534Au /*JSON*/ && !have_json) {
                char *text = (char *)sdga_alloc(&m->arena, clen + 1);
                if (!text) goto fail;
                memcpy(text, body, clen);
                text[clen] = '\0';
                json = text;
                have_json = 1;
            } else if (ctype == 0x004E4942u /*BIN\0*/ && !bufs.bin) {
                unsigned char *bin = (unsigned char *)sdga_alloc(&m->arena,
                                                                 clen ? clen : 1);
                if (!bin) goto fail;
                memcpy(bin, body, clen);
                bufs.bin = bin;
                bufs.bin_len = clen;
            }
            pos += 8 + clen + ((clen & 3) ? (4 - (clen & 3)) : 0);
        }
        if (!have_json) goto fail;
    } else {
        char *text = (char *)sdga_alloc(&m->arena, file_size + 1);
        if (!text) goto fail;
        memcpy(text, file, file_size);
        text[file_size] = '\0';
        json = text;
    }
    free(file);
    file = NULL;

    /* ---- parse JSON ---- */
    sdj_token *toks = (sdj_token *)malloc(sizeof(sdj_token) * SDJ_MAX_TOKENS);
    if (!toks) goto fail;
    sdj_doc doc;
    sdj_init(&doc, json, toks);
    if (doc.count <= 0 || doc.tok[0].type != SDJ_OBJECT) {
        free(toks);
        fprintf(stderr, "gltf: bad JSON (%d)\n", doc.count);
        goto fail;
    }

    {
        int ext = sdj_obj_get(&doc, 0, "extensionsRequired");
        if (sdj_len(&doc, ext) > 0) {
            fprintf(stderr, "gltf: model requires unsupported extensions\n");
            free(toks);
            goto fail;
        }
    }

    if (!is_glb && !bufs.bin) {
        if (g_load_buffer0(&doc, path, m, &bufs) != 0) {
            /* model without binary data - keep going */
        }
    }

    /* ---- NEW: bufferViews (needed for embedded images) ---- */
    {
        int arr = sdj_obj_get(&doc, 0, "bufferViews");
        m->buffer_view_count = sdj_len(&doc, arr);
        if (m->buffer_view_count > 0) {
            m->buffer_views = (sdge_gltf_buffer_view *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_buffer_view) * (size_t)m->buffer_view_count);
            if (!m->buffer_views) { free(toks); goto fail; }
            memset(m->buffer_views, 0,
                   sizeof(sdge_gltf_buffer_view) * (size_t)m->buffer_view_count);
            for (int i = 0; i < m->buffer_view_count; ++i) {
                int vt = sdj_arr_at(&doc, arr, i);
                int off = (int)sdj_num(&doc, sdj_obj_get(&doc, vt, "byteOffset"));
                int len = (int)sdj_num(&doc, sdj_obj_get(&doc, vt, "byteLength"));
                int stride = (int)sdj_num(&doc, sdj_obj_get(&doc, vt, "byteStride"));
                int buf_idx = (int)sdj_num(&doc, sdj_obj_get(&doc, vt, "buffer"));
                if (buf_idx != 0 || !bufs.bin) continue;
                if (off < 0 || len <= 0) continue;
                if ((size_t)off + (size_t)len > bufs.bin_len) continue;
                m->buffer_views[i].data = bufs.bin + off;
                m->buffer_views[i].byte_length = len;
                m->buffer_views[i].byte_offset = off;
                m->buffer_views[i].byte_stride = stride;
            }
        }
    }

    /* ---- nodes ---- */
    {
        int arr = sdj_obj_get(&doc, 0, "nodes");
        m->node_count = sdj_len(&doc, arr);
        if (m->node_count > 0) {
            m->nodes = (sdge_gltf_node *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_node) * (size_t)m->node_count);
            if (!m->nodes) { free(toks); goto fail; }
            memset(m->nodes, 0, sizeof(sdge_gltf_node) * (size_t)m->node_count);
        }
        for (int i = 0; i < m->node_count; ++i) {
            int nt = sdj_arr_at(&doc, arr, i);
            sdge_gltf_node *n = &m->nodes[i];
            n->parent = -1;
            n->mesh = -1;
            n->skin = -1;
            n->name = g_str(m, &doc, sdj_obj_get(&doc, nt, "name"));
            v3_set(n->translation, 0, 0, 0);
            q_identity(n->rotation);
            v3_set(n->scale, 1, 1, 1);

            int mat = sdj_obj_get(&doc, nt, "matrix");
            if (mat >= 0 && sdj_len(&doc, mat) == 16) {
                for (int k = 0; k < 16; ++k)
                    n->matrix[k] = (float)sdj_num(&doc, sdj_arr_at(&doc, mat, k));
                n->has_matrix = 1;
            } else {
                int t = sdj_obj_get(&doc, nt, "translation");
                int r = sdj_obj_get(&doc, nt, "rotation");
                int s = sdj_obj_get(&doc, nt, "scale");
                if (t >= 0)
                    for (int k = 0; k < 3; ++k)
                        n->translation[k] = (float)sdj_num(&doc, sdj_arr_at(&doc, t, k));
                if (r >= 0) {
                    for (int k = 0; k < 4; ++k)
                        n->rotation[k] = (float)sdj_num(&doc, sdj_arr_at(&doc, r, k));
                    q_norm(n->rotation, n->rotation);
                }
                if (s >= 0)
                    for (int k = 0; k < 3; ++k)
                        n->scale[k] = (float)sdj_num(&doc, sdj_arr_at(&doc, s, k));
                m4_from_trs(n->matrix, n->translation, n->rotation, n->scale);
            }

            n->mesh = (int)sdj_num(&doc, sdj_obj_get(&doc, nt, "mesh"));
            if (sdj_obj_get(&doc, nt, "mesh") < 0) n->mesh = -1;
            n->skin = (int)sdj_num(&doc, sdj_obj_get(&doc, nt, "skin"));
            if (sdj_obj_get(&doc, nt, "skin") < 0) n->skin = -1;

            int ch = sdj_obj_get(&doc, nt, "children");
            n->child_count = sdj_len(&doc, ch);
            if (n->child_count > 0) {
                n->children = (int *)sdga_alloc(&m->arena,
                    sizeof(int) * (size_t)n->child_count);
                if (!n->children) { free(toks); goto fail; }
                for (int k = 0; k < n->child_count; ++k)
                    n->children[k] = (int)sdj_num(&doc, sdj_arr_at(&doc, ch, k));
            }
        }
        for (int i = 0; i < m->node_count; ++i)
            for (int k = 0; k < m->nodes[i].child_count; ++k) {
                int c = m->nodes[i].children[k];
                if (c >= 0 && c < m->node_count) m->nodes[c].parent = i;
            }
    }

    /* ---- scene roots ---- */
    {
        int scene_idx = (int)sdj_num(&doc, sdj_obj_get(&doc, 0, "scene"));
        int scenes = sdj_obj_get(&doc, 0, "scenes");
        int st = sdj_arr_at(&doc, scenes, scene_idx);
        int roots_tok = sdj_obj_get(&doc, st, "nodes");
        m->root_count = sdj_len(&doc, roots_tok);
        if (m->root_count <= 0) {
            for (int i = 0; i < m->node_count; ++i)
                if (m->nodes[i].parent < 0) m->root_count++;
        }
        m->roots = (int *)sdga_alloc(&m->arena,
                                     sizeof(int) * (size_t)(m->root_count + 1));
        if (!m->roots) { free(toks); goto fail; }
        int w = 0;
        if (sdj_len(&doc, roots_tok) > 0) {
            for (int i = 0; i < m->root_count; ++i)
                m->roots[w++] = (int)sdj_num(&doc, sdj_arr_at(&doc, roots_tok, i));
        } else {
            for (int i = 0; i < m->node_count; ++i)
                if (m->nodes[i].parent < 0) m->roots[w++] = i;
            m->root_count = w;
        }
    }

    /* ---- meshes ---- */
    {
        int arr = sdj_obj_get(&doc, 0, "meshes");
        m->mesh_count = sdj_len(&doc, arr);
        if (m->mesh_count > 0) {
            m->meshes = (sdge_gltf_mesh *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_mesh) * (size_t)m->mesh_count);
            if (!m->meshes) { free(toks); goto fail; }
            memset(m->meshes, 0, sizeof(sdge_gltf_mesh) * (size_t)m->mesh_count);
        }
        for (int i = 0; i < m->mesh_count; ++i) {
            int mt = sdj_arr_at(&doc, arr, i);
            sdge_gltf_mesh *mesh = &m->meshes[i];
            mesh->name = g_str(m, &doc, sdj_obj_get(&doc, mt, "name"));
            int prims = sdj_obj_get(&doc, mt, "primitives");
            mesh->prim_count = sdj_len(&doc, prims);
            if (mesh->prim_count <= 0) continue;
            mesh->prims = (sdge_primitive *)sdga_alloc(&m->arena,
                sizeof(sdge_primitive) * (size_t)mesh->prim_count);
            if (!mesh->prims) { free(toks); goto fail; }
            memset(mesh->prims, 0,
                   sizeof(sdge_primitive) * (size_t)mesh->prim_count);

            for (int p = 0; p < mesh->prim_count; ++p) {
                int pt = sdj_arr_at(&doc, prims, p);
                sdge_primitive *prim = &mesh->prims[p];
                prim->material = -1;
                int mode_tok = sdj_obj_get(&doc, pt, "mode");
                prim->mode = mode_tok >= 0 ? (int)sdj_num(&doc, mode_tok) : 4;
                if (prim->mode != 4) {
                    fprintf(stderr, "gltf: mesh %d prim %d: mode %d unsupported "
                            "(only triangles)\n", i, p, prim->mode);
                    continue;
                }
                int mat_tok = sdj_obj_get(&doc, pt, "material");
                prim->material = mat_tok >= 0 ? (int)sdj_num(&doc, mat_tok) : -1;

                int attrs = sdj_obj_get(&doc, pt, "attributes");
                int pairs = sdj_len(&doc, attrs) / 2;
                int at = attrs + 1;
                for (int ap = 0; ap < pairs && at < doc.count; ++ap) {
                    char key[32];
                    sdj_str(&doc, at, key, (int)sizeof(key));
                    int acc_idx = (int)sdj_num(&doc, at + 1);
                    int acc = sdj_arr_at(&doc,
                            sdj_obj_get(&doc, 0, "accessors"), acc_idx);
                    sdge_accessor *dst = NULL;
                    if      (!strcmp(key, "POSITION"))  dst = &prim->positions;
                    else if (!strcmp(key, "NORMAL"))    dst = &prim->normals;
                    else if (!strcmp(key, "TEXCOORD_0")) dst = &prim->texcoords;
                    else if (!strcmp(key, "TEXCOORD_1")) dst = &prim->texcoords1;
                    else if (!strcmp(key, "TANGENT"))   dst = &prim->tangents;
                    else if (!strcmp(key, "COLOR_0"))    dst = &prim->colors;
                    else if (!strcmp(key, "JOINTS_0"))  dst = &prim->joints;
                    else if (!strcmp(key, "WEIGHTS_0")) dst = &prim->weights;
                    if (dst && acc >= 0) {
                        if (g_parse_accessor(&doc, acc, &bufs, dst) != 0) {
                            fprintf(stderr, "gltf: bad accessor for %s\n", key);
                            memset(dst, 0, sizeof(*dst));
                        }
                    }
                    at += 1 + sdj_subtree_tokens(&doc, at + 1);
                }
                prim->has_skin = (prim->joints.count > 0
                               && prim->weights.count > 0);

                int idx_tok = sdj_obj_get(&doc, pt, "indices");
                if (idx_tok >= 0) {
                    int acc_idx = (int)sdj_num(&doc, idx_tok);
                    int acc = sdj_arr_at(&doc,
                            sdj_obj_get(&doc, 0, "accessors"), acc_idx);
                    if (g_parse_accessor(&doc, acc, &bufs, &prim->indices) != 0)
                        memset(&prim->indices, 0, sizeof(prim->indices));
                }
                if (prim->positions.count == 0) {
                    fprintf(stderr, "gltf: mesh %d prim %d has no POSITION\n",
                            i, p);
                }
            }
        }
    }

    /* ---- skins ---- */
    {
        int arr = sdj_obj_get(&doc, 0, "skins");
        m->skin_count = sdj_len(&doc, arr);
        if (m->skin_count > 0) {
            m->skins = (sdge_gltf_skin *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_skin) * (size_t)m->skin_count);
            if (!m->skins) { free(toks); goto fail; }
            memset(m->skins, 0, sizeof(sdge_gltf_skin) * (size_t)m->skin_count);
        }
        for (int i = 0; i < m->skin_count; ++i) {
            int st = sdj_arr_at(&doc, arr, i);
            sdge_gltf_skin *sk = &m->skins[i];
            int jt = sdj_obj_get(&doc, st, "joints");
            sk->joint_count = sdj_len(&doc, jt);
            sk->skeleton_root = -1;
            if (sk->joint_count <= 0) continue;
            sk->joints = (int *)sdga_alloc(&m->arena,
                sizeof(int) * (size_t)sk->joint_count);
            if (!sk->joints) { free(toks); goto fail; }
            for (int k = 0; k < sk->joint_count; ++k)
                sk->joints[k] = (int)sdj_num(&doc, sdj_arr_at(&doc, jt, k));
            int sr = sdj_obj_get(&doc, st, "skeleton");
            sk->skeleton_root = sr >= 0 ? (int)sdj_num(&doc, sr) : -1;

            int ibm_idx = (int)sdj_num(&doc, sdj_obj_get(&doc, st,
                                                         "inverseBindMatrices"));
            int ibm_acc = sdj_arr_at(&doc, sdj_obj_get(&doc, 0, "accessors"),
                                     ibm_idx);
            sdge_accessor a;
            if (ibm_acc >= 0 && g_parse_accessor(&doc, ibm_acc, &bufs, &a) == 0
                && a.comps == 16) {
                sk->inverse_bind = g_accessor_to_f32(m, &a);
            }
        }
    }

    /* ---- animations ---- */
    {
        int arr = sdj_obj_get(&doc, 0, "animations");
        m->anim_count = sdj_len(&doc, arr);
        if (m->anim_count > 0) {
            m->anims = (sdge_gltf_animation *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_animation) * (size_t)m->anim_count);
            if (!m->anims) { free(toks); goto fail; }
            memset(m->anims, 0,
                   sizeof(sdge_gltf_animation) * (size_t)m->anim_count);
        }
        for (int i = 0; i < m->anim_count; ++i) {
            int at = sdj_arr_at(&doc, arr, i);
            sdge_gltf_animation *an = &m->anims[i];
            an->name = g_str(m, &doc, sdj_obj_get(&doc, at, "name"));
            int chans = sdj_obj_get(&doc, at, "channels");
            int samplers = sdj_obj_get(&doc, at, "samplers");
            an->channel_count = sdj_len(&doc, chans);
            if (an->channel_count <= 0) continue;
            an->channels = (sdge_gltf_channel *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_channel) * (size_t)an->channel_count);
            if (!an->channels) { free(toks); goto fail; }
            memset(an->channels, 0,
                   sizeof(sdge_gltf_channel) * (size_t)an->channel_count);

            for (int c = 0; c < an->channel_count; ++c) {
                int ct = sdj_arr_at(&doc, chans, c);
                sdge_gltf_channel *ch = &an->channels[c];
                int target = sdj_obj_get(&doc, ct, "target");
                ch->node = (int)sdj_num(&doc,
                                        sdj_obj_get(&doc, target, "node"));
                if (sdj_obj_get(&doc, target, "node") < 0) ch->node = -1;
                char pbuf[16];
                sdj_str(&doc, sdj_obj_get(&doc, target, "path"),
                        pbuf, (int)sizeof(pbuf));
                if      (!strcmp(pbuf, "translation")) ch->path = SDGE_ANIM_T;
                else if (!strcmp(pbuf, "rotation"))   ch->path = SDGE_ANIM_R;
                else if (!strcmp(pbuf, "scale"))      ch->path = SDGE_ANIM_S;
                else ch->path = SDGE_ANIM_W;

                int sidx = (int)sdj_num(&doc, sdj_obj_get(&doc, ct, "sampler"));
                int smp = sdj_arr_at(&doc, samplers, sidx);
                sdge_accessor tin, tout;
                int in_acc = sdj_arr_at(&doc, sdj_obj_get(&doc, 0, "accessors"),
                    (int)sdj_num(&doc, sdj_obj_get(&doc, smp, "input")));
                int out_acc = sdj_arr_at(&doc, sdj_obj_get(&doc, 0, "accessors"),
                    (int)sdj_num(&doc, sdj_obj_get(&doc, smp, "output")));
                if (g_parse_accessor(&doc, in_acc, &bufs, &tin) != 0
                 || g_parse_accessor(&doc, out_acc, &bufs, &tout) != 0)
                    continue;
                char ibuf[16];
                sdj_str(&doc, sdj_obj_get(&doc, smp, "interpolation"),
                        ibuf, (int)sizeof(ibuf));
                ch->interp = SDGE_ANIM_LINEAR;
                if (!strcmp(ibuf, "STEP"))       ch->interp = SDGE_ANIM_STEP;
                else if (!strcmp(ibuf, "CUBICSPLINE")) ch->interp = SDGE_ANIM_CUBIC;

                ch->key_count = tin.count;
                ch->comps = tout.comps;
                ch->times  = g_accessor_to_f32(m, &tin);
                ch->values = g_accessor_to_f32(m, &tout);
                if (ch->times && ch->key_count > 0) {
                    float tlast = ch->times[ch->key_count - 1];
                    if (tlast > an->duration) an->duration = tlast;
                }
            }
        }
    }

    /* ---- NEW: images / textures / samplers ---- */
    {
        int arr = sdj_obj_get(&doc, 0, "images");
        m->image_count = sdj_len(&doc, arr);
        if (m->image_count > 0) {
            m->images = (sdge_gltf_image *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_image) * (size_t)m->image_count);
            if (!m->images) { free(toks); goto fail; }
            memset(m->images, 0, sizeof(sdge_gltf_image) * (size_t)m->image_count);
            for (int i = 0; i < m->image_count; ++i) {
                int it = sdj_arr_at(&doc, arr, i);
                sdge_gltf_image *im = &m->images[i];
                im->buffer_view = -1;
                im->name = g_str(m, &doc, sdj_obj_get(&doc, it, "name"));
                im->mime_type = g_str(m, &doc, sdj_obj_get(&doc, it, "mimeType"));
                int bv = sdj_obj_get(&doc, it, "bufferView");
                if (bv >= 0) im->buffer_view = (int)sdj_num(&doc, bv);
                int uri = sdj_obj_get(&doc, it, "uri");
                if (uri >= 0) {
                    int ulen = 0;
                    const char *raw = g_str_raw(&doc, uri, &ulen);
                    if (raw && ulen > 5 && !strncmp(raw, "data:", 5)) {
                        /* embedded base64 payload: decode it right away */
                        size_t dlen = 0;
                        unsigned char *dec = g_data_uri_decode(raw,
                                                               (size_t)ulen, &dlen);
                        if (dec && dlen > 0) {
                            unsigned char *copy = (unsigned char *)
                                sdga_alloc(&m->arena, dlen);
                            if (copy) {
                                memcpy(copy, dec, dlen);
                                im->data = copy;
                                im->data_len = (int)dlen;
                            }
                        }
                        free(dec);
                    } else if (raw) {
                        im->uri = g_strdup_raw(m, &doc, uri, &im->uri_len);
                    }
                }
            }
        }
    }
    {
        int arr = sdj_obj_get(&doc, 0, "textures");
        m->texture_count = sdj_len(&doc, arr);
        if (m->texture_count > 0) {
            m->textures = (sdge_gltf_texture *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_texture) * (size_t)m->texture_count);
            if (!m->textures) { free(toks); goto fail; }
            memset(m->textures, 0,
                   sizeof(sdge_gltf_texture) * (size_t)m->texture_count);
            for (int i = 0; i < m->texture_count; ++i) {
                int tt = sdj_arr_at(&doc, arr, i);
                sdge_gltf_texture *tx = &m->textures[i];
                tx->source = -1;
                tx->sampler = -1;
                int s = sdj_obj_get(&doc, tt, "source");
                if (s >= 0) tx->source = (int)sdj_num(&doc, s);
                int sm = sdj_obj_get(&doc, tt, "sampler");
                if (sm >= 0) tx->sampler = (int)sdj_num(&doc, sm);
            }
        }
    }
    {
        int arr = sdj_obj_get(&doc, 0, "samplers");
        m->sampler_count = sdj_len(&doc, arr);
        if (m->sampler_count > 0) {
            m->samplers = (sdge_gltf_sampler *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_sampler) * (size_t)m->sampler_count);
            if (!m->samplers) { free(toks); goto fail; }
            memset(m->samplers, 0,
                   sizeof(sdge_gltf_sampler) * (size_t)m->sampler_count);
            for (int i = 0; i < m->sampler_count; ++i) {
                int st = sdj_arr_at(&doc, arr, i);
                sdge_gltf_sampler *sp = &m->samplers[i];
                sp->mag_filter = (int)sdj_num(&doc, sdj_obj_get(&doc, st, "magFilter"));
                sp->min_filter = (int)sdj_num(&doc, sdj_obj_get(&doc, st, "minFilter"));
                sp->wrap_s = (int)sdj_num(&doc, sdj_obj_get(&doc, st, "wrapS"));
                sp->wrap_t = (int)sdj_num(&doc, sdj_obj_get(&doc, st, "wrapT"));
            }
        }
    }

    /* ---- materials ---- */
    {
        int arr = sdj_obj_get(&doc, 0, "materials");
        m->material_count = sdj_len(&doc, arr);
        if (m->material_count > 0) {
            m->materials = (sdge_gltf_material *)sdga_alloc(&m->arena,
                sizeof(sdge_gltf_material) * (size_t)m->material_count);
            if (!m->materials) { free(toks); goto fail; }
            memset(m->materials, 0,
                   sizeof(sdge_gltf_material) * (size_t)m->material_count);
        }
        for (int i = 0; i < m->material_count; ++i) {
            int mt = sdj_arr_at(&doc, arr, i);
            sdge_gltf_material *mat = &m->materials[i];
            mat->name = g_str(m, &doc, sdj_obj_get(&doc, mt, "name"));
            v4_set(mat->base_color, 1.0f, 1.0f, 1.0f, 1.0f);
            v3_set(mat->emissive, 0.0f, 0.0f, 0.0f);
            mat->metallic = 1.0f;
            mat->roughness = 1.0f;
            mat->double_sided = 0;
            mat->alpha_mode = SDGE_ALPHA_OPAQUE;
            mat->alpha_cutoff = 0.5f;

            int pbr = sdj_obj_get(&doc, mt, "pbrMetallicRoughness");
            int bc = sdj_obj_get(&doc, pbr, "baseColorFactor");
            if (sdj_len(&doc, bc) == 4)
                for (int k = 0; k < 4; ++k)
                    mat->base_color[k] = (float)sdj_num(&doc,
                                                        sdj_arr_at(&doc, bc, k));
            int mf = sdj_obj_get(&doc, pbr, "metallicFactor");
            if (mf >= 0) mat->metallic  = (float)sdj_num(&doc, mf);
            int rf = sdj_obj_get(&doc, pbr, "roughnessFactor");
            if (rf >= 0) mat->roughness = (float)sdj_num(&doc, rf);

            int ds = sdj_obj_get(&doc, mt, "doubleSided");
            if (ds >= 0) mat->double_sided = (doc.src[doc.tok[ds].start] == 't');

            int ef = sdj_obj_get(&doc, mt, "emissiveFactor");
            if (sdj_len(&doc, ef) == 3)
                for (int k = 0; k < 3; ++k)
                    mat->emissive[k] = (float)sdj_num(&doc,
                                                     sdj_arr_at(&doc, ef, k));

            int am = sdj_obj_get(&doc, mt, "alphaMode");
            if (am >= 0) {
                char abuf[16];
                sdj_str(&doc, am, abuf, (int)sizeof(abuf));
                if      (!strcmp(abuf, "MASK"))  mat->alpha_mode = SDGE_ALPHA_MASK;
                else if (!strcmp(abuf, "BLEND")) mat->alpha_mode = SDGE_ALPHA_BLEND;
                else                             mat->alpha_mode = SDGE_ALPHA_OPAQUE;
            }
            int ac = sdj_obj_get(&doc, mt, "alphaCutoff");
            if (ac >= 0) mat->alpha_cutoff = (float)sdj_num(&doc, ac);

            /* ---- texture slots ---- */
            g_parse_texinfo(&doc, sdj_obj_get(&doc, pbr, "baseColorTexture"),
                            m->texture_count, &mat->base_color_tex);
            g_parse_texinfo(&doc,
                            sdj_obj_get(&doc, pbr, "metallicRoughnessTexture"),
                            m->texture_count, &mat->metallic_roughness_tex);
            g_parse_texinfo(&doc, sdj_obj_get(&doc, mt, "normalTexture"),
                            m->texture_count, &mat->normal_tex);
            g_parse_texinfo(&doc, sdj_obj_get(&doc, mt, "occlusionTexture"),
                            m->texture_count, &mat->occlusion_tex);
            g_parse_texinfo(&doc, sdj_obj_get(&doc, mt, "emissiveTexture"),
                            m->texture_count, &mat->emissive_tex);
        }
    }

    free(toks);
    return m;

fail:
    free(file);
    sdge_gltf_destroy(m);
    return NULL;
}

#endif /* SDGE_GLTF_IMPLEMENTATION */

#endif /* SDGE_GLTF_H */