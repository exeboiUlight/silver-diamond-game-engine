/* gltf_save.c - Silver Diamond Game Engine :: the glTF writer of the editor
 *
 * Writes the edited model as a new glTF 2.0 file:
 *   - the mesh data goes into <name>.bin (the accessors are written as
 *     float32 / uint32, so every loader can read them);
 *   - the images are embedded as data URIs (base64), so the result is a
 *     pair of files that always works;
 *   - the nodes keep their hierarchy and their (edited) transforms;
 *   - a node with its own texture gets its own copy of the mesh and of the
 *     material, so the texture really belongs to this node only.
 */
#include "editor.h"

#include "../assets/assets.h"
#include "../graphics/renderer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

/* a copy of a text (strdup is not part of C99) */
static char *dup_text(const char *text) {
    if (!text) text = "";
    size_t len = strlen(text);
    char *out = (char *)malloc(len + 1u);
    if (!out) return NULL;
    memcpy(out, text, len + 1u);
    return out;
}

/* ------------------------------------------------------------- the buffer */

typedef struct {
    unsigned char *data;
    size_t size;
    size_t cap;
} binbuf;

static void bin_free(binbuf *b) {
    if (!b) return;
    free(b->data);
    memset(b, 0, sizeof(*b));
}

static int bin_reserve(binbuf *b, size_t bytes) {
    if (b->size + bytes <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 4096;
    while (cap < b->size + bytes) cap *= 2;
    unsigned char *d = (unsigned char *)realloc(b->data, cap);
    if (!d) return -1;
    b->data = d;
    b->cap = cap;
    return 0;
}

/* writes the data and returns the offset (the buffer is aligned to 4) */
static long bin_write(binbuf *b, const void *src, size_t bytes) {
    while (b->size % 4) {
        if (bin_reserve(b, 1) != 0) return -1;
        b->data[b->size++] = 0;
    }
    if (bin_reserve(b, bytes) != 0) return -1;
    long off = (long)b->size;
    if (src) memcpy(b->data + b->size, src, bytes);
    b->size += bytes;
    return off;
}

/* ------------------------------------------------------------ the json text */

typedef struct {
    char  *data;
    size_t size;
    size_t cap;
} strbuf;

static void str_free(strbuf *s) {
    if (!s) return;
    free(s->data);
    memset(s, 0, sizeof(*s));
}

static void str_add(strbuf *s, const char *text) {
    if (!s || !text) return;
    size_t len = strlen(text);
    if (s->size + len + 1 > s->cap) {
        size_t cap = s->cap ? s->cap : 8192;
        while (cap < s->size + len + 1) cap *= 2;
        char *d = (char *)realloc(s->data, cap);
        if (!d) return;
        s->data = d;
        s->cap = cap;
    }
    memcpy(s->data + s->size, text, len);
    s->size += len;
    s->data[s->size] = '\0';
}

static void str_addf(strbuf *s, const char *fmt, ...) {
    char tmp[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    str_add(s, tmp);
}

/* the JSON string of a text (the quotes and the backslashes are escaped) */
static void str_add_json(strbuf *s, const char *text) {
    str_add(s, "\"");
    for (const char *p = text ? text : ""; *p; ++p) {
        char buf[4];
        buf[0] = buf[1] = buf[2] = buf[3] = 0;
        if (*p == '"' || *p == '\\') {
            buf[0] = '\\';
            buf[1] = *p;
        } else if ((unsigned char)*p < 0x20) {
            buf[0] = '\\';
            buf[1] = 'u';
            buf[2] = '0';
        } else {
            buf[0] = *p;
        }
        str_add(s, buf);
    }
    str_add(s, "\"");
}

/* --------------------------------------------------------------- base64 */

static const char B64[] =
"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(strbuf *out, const unsigned char *data, size_t len) {
    size_t i = 0;
    while (i + 2 < len) {
        unsigned v = ((unsigned)data[i] << 16) | ((unsigned)data[i + 1] << 8) |
                     data[i + 2];
        char q[4];
        q[0] = B64[(v >> 18) & 63];
        q[1] = B64[(v >> 12) & 63];
        q[2] = B64[(v >> 6) & 63];
        q[3] = B64[v & 63];
        char buf[5] = { q[0], q[1], q[2], q[3], 0 };
        str_add(out, buf);
        i += 3;
    }
    if (i < len) {
        unsigned v = (unsigned)data[i] << 16;
        int rem = 1;
        if (i + 1 < len) { v |= (unsigned)data[i + 1] << 8; rem = 2; }
        char q[4];
        q[0] = B64[(v >> 18) & 63];
        q[1] = B64[(v >> 12) & 63];
        q[2] = (rem == 2) ? B64[(v >> 6) & 63] : '=';
        q[3] = '=';
        char buf[5] = { q[0], q[1], q[2], q[3], 0 };
        str_add(out, buf);
    }
}

/* ---------------------------------------------------------------- the data */

typedef struct {
    int count;
    int comps;          /* 1 = scalar, 2 = vec2, 3 = vec3, 4 = vec4 */
    int ctype;          /* 5126 = float, 5125 = uint */
    int view;           /* the index of the bufferView */
    float min[3];
    float max[3];
    int has_bounds;
} exp_accessor;

typedef struct {
    int  gltf_mesh;     /* the mesh of the original file */
    int  material[16];  /* the material of every primitive */
    int  prim_count;
    int  node;          /* the node it was made for (-1 = the original) */
    int  acc[4];        /* the accessors (position, normal, uv, index) */
} exp_mesh;

typedef struct {
    int image;          /* the image of the texture */
} exp_texture;

typedef struct {
    int base_color, mr, normal, occlusion, emissive;  /* the textures */
    float base_color_f[4];
    float metallic, roughness;
    float emissive_f[3];
    float normal_scale, occlusion_strength;
    float alpha_cutoff;
    int   alpha_mode;
    int   double_sided;
    int   from_gltf;    /* the material of the original file (-1 = new) */
} exp_material;

/* the export state */
typedef struct {
    sdge_editor *ed;
    const sdge_gltf_model *g;
    char dir[512];            /* the directory of the original file */

    binbuf bin;
    strbuf json;

    exp_accessor *accs;   int acc_count,  acc_cap;
    int  *views;          int view_count, view_cap;   /* the offsets */
    int  *view_lens;
    exp_mesh    *meshes;  int mesh_count, mesh_cap;
    exp_material *mats;   int mat_count,  mat_cap;
    exp_texture *texs;    int tex_count,  tex_cap;
    char **img_uris;      int img_count;
} export_state;

/* reads one component of a glTF accessor */
static float acc_read(const sdge_accessor *a, int index, int comp) {
    if (!a || !a->data || index < 0 || index >= a->count) return 0.0f;
    int csz = 4;
    switch (a->ctype) {
    case SDGE_GLTF_FLOAT: csz = 4; break;
    case SDGE_GLTF_UNSIGNED_SHORT: csz = 2; break;
    case SDGE_GLTF_SHORT: csz = 2; break;
    case SDGE_GLTF_UNSIGNED_BYTE: csz = 1; break;
    case SDGE_GLTF_BYTE: csz = 1; break;
    default: return 0.0f;
    }
    const unsigned char *p = a->data + (size_t)index * (size_t)a->stride +
                             (size_t)comp * (size_t)csz;
    switch (a->ctype) {
    case SDGE_GLTF_FLOAT: { float f; memcpy(&f, p, 4); return f; }
    case SDGE_GLTF_UNSIGNED_SHORT: {
        unsigned short v; memcpy(&v, p, 2);
        return a->normalized ? (float)v / 65535.0f : (float)v;
    }
    case SDGE_GLTF_SHORT: {
        short v; memcpy(&v, p, 2);
        return a->normalized ? (float)v / 32767.0f : (float)v;
    }
    case SDGE_GLTF_UNSIGNED_BYTE:
        return a->normalized ? (float)p[0] / 255.0f : (float)p[0];
    case SDGE_GLTF_BYTE:
        return a->normalized ? (float)(signed char)p[0] / 127.0f
                              : (float)(signed char)p[0];
    default: break;
    }
    return 0.0f;
}

static unsigned acc_read_index(const sdge_accessor *a, int i) {
    if (!a || !a->data || i < 0 || i >= a->count) return 0;
    const unsigned char *p = a->data + (size_t)i * (size_t)a->stride;
    switch (a->ctype) {
    case SDGE_GLTF_UNSIGNED_INT: { unsigned v; memcpy(&v, p, 4); return v; }
    case SDGE_GLTF_UNSIGNED_SHORT: { unsigned short v; memcpy(&v, p, 2); return v; }
    case SDGE_GLTF_UNSIGNED_BYTE: return p[0];
    default: break;
    }
    return 0;
}

/* --------------------------------------------------------- the collections */

static int exp_acc_add(export_state *st, exp_accessor a) {
    if (st->acc_count >= st->acc_cap) {
        int cap = st->acc_cap ? st->acc_cap * 2 : 64;
        exp_accessor *n = (exp_accessor *)realloc(st->accs,
                             sizeof(exp_accessor) * (size_t)cap);
        if (!n) return -1;
        st->accs = n;
        st->acc_cap = cap;
    }
    st->accs[st->acc_count] = a;
    return st->acc_count++;
}

static int exp_view_add(export_state *st, long offset, int length) {
    if (st->view_count >= st->view_cap) {
        int cap = st->view_cap ? st->view_cap * 2 : 64;
        int *no = (int *)realloc(st->views, sizeof(int) * (size_t)cap);
        int *nl = (int *)realloc(st->view_lens, sizeof(int) * (size_t)cap);
        if (!no || !nl) { free(no); free(nl); return -1; }
        st->views = no;
        st->view_lens = nl;
        st->view_cap = cap;
    }
    st->views[st->view_count] = (int)offset;
    st->view_lens[st->view_count] = length;
    return st->view_count++;
}

static int exp_mesh_add(export_state *st, exp_mesh m) {
    if (st->mesh_count >= st->mesh_cap) {
        int cap = st->mesh_cap ? st->mesh_cap * 2 : 16;
        exp_mesh *n = (exp_mesh *)realloc(st->meshes,
                                          sizeof(exp_mesh) * (size_t)cap);
        if (!n) return -1;
        st->meshes = n;
        st->mesh_cap = cap;
    }
    st->meshes[st->mesh_count] = m;
    return st->mesh_count++;
}

static int exp_mat_add(export_state *st, exp_material m) {
    if (st->mat_count >= st->mat_cap) {
        int cap = st->mat_cap ? st->mat_cap * 2 : 16;
        exp_material *n = (exp_material *)realloc(st->mats,
                              sizeof(exp_material) * (size_t)cap);
        if (!n) return -1;
        st->mats = n;
        st->mat_cap = cap;
    }
    st->mats[st->mat_count] = m;
    return st->mat_count++;
}

static int exp_tex_add(export_state *st, exp_texture t) {
    if (st->tex_count >= st->tex_cap) {
        int cap = st->tex_cap ? st->tex_cap * 2 : 16;
        exp_texture *n = (exp_texture *)realloc(st->texs,
                             sizeof(exp_texture) * (size_t)cap);
        if (!n) return -1;
        st->texs = n;
        st->tex_cap = cap;
    }
    st->texs[st->tex_count] = t;
    return st->tex_count++;
}

static int exp_img_add(export_state *st, const char *uri) {
    char **n = (char **)realloc(st->img_uris,
                                sizeof(char *) * (size_t)(st->img_count + 1));
    if (!n) return -1;
    st->img_uris = n;
    st->img_uris[st->img_count] = dup_text(uri ? uri : "");
    if (!st->img_uris[st->img_count]) return -1;
    return st->img_count++;
}

/* ------------------------------------------------------- the images */

/* the directory of a file path (with the separator at the end) */
static void dir_of(const char *path, char *out, size_t size) {
    out[0] = '\0';
    if (!path) return;
    const char *base = path;
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    size_t len = (size_t)(base - path);
    if (len >= size) len = size - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

/* the path of a file that is relative to the model */
static void resolve_path(const char *dir, const char *uri, char *out,
                         size_t size) {
    if (!uri) { out[0] = '\0'; return; }
    if (uri[0] == '/' || uri[0] == '\\' || !dir || !dir[0]) {
        snprintf(out, size, "%s", uri);
        return;
    }
    snprintf(out, size, "%s%s", dir, uri);
}

/* the bytes of an image of the original file (the caller frees them) */
static unsigned char *image_bytes(const sdge_gltf_model *g,
                                  const sdge_gltf_image *im, int *size_out,
                                  const char *dir) {
    *size_out = 0;
    if (!im) return NULL;
    if (im->data && im->data_len > 0) {
        unsigned char *copy = (unsigned char *)malloc((size_t)im->data_len);
        if (!copy) return NULL;
        memcpy(copy, im->data, (size_t)im->data_len);
        *size_out = im->data_len;
        return copy;
    }
    if (im->buffer_view >= 0 && im->buffer_view < g->buffer_view_count) {
        const sdge_gltf_buffer_view *bv = &g->buffer_views[im->buffer_view];
        if (bv->data && bv->byte_length > 0) {
            unsigned char *copy =
                (unsigned char *)malloc((size_t)bv->byte_length);
            if (!copy) return NULL;
            memcpy(copy, bv->data, (size_t)bv->byte_length);
            *size_out = bv->byte_length;
            return copy;
        }
    }
    if (im->uri) {
        unsigned char *data = NULL;
        size_t size = 0;
        char full[1024];
        resolve_path(dir, im->uri, full, sizeof(full));
        if (sdga_read_file(full, &data, &size) == 0 && data) {
            *size_out = (int)size;
            return data;
        }
    }
    return NULL;
}

/* the mime type of an image (by the file name) */
static const char *mime_of(const char *path) {
    if (!path) return "image/png";
    const char *dot = strrchr(path, '.');
    if (!dot) return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcmp(dot, ".bmp")) return "image/bmp";
    if (!strcmp(dot, ".tga")) return "image/x-targa";
    return "image/png";
}

/* the image of the original file (embedded as a data URI) */
static int export_image(export_state *st, const sdge_gltf_image *im,
                        const char *file) {
    unsigned char *bytes = NULL;
    int size = 0;
    const char *mime = "image/png";

    if (file) {
        size_t file_size = 0;
        if (sdga_read_file(file, &bytes, &file_size) != 0 || !bytes)
            return -1;
        size = (int)file_size;
        mime = mime_of(file);
    } else {
        bytes = image_bytes(st->g, im, &size, st->dir);
        if (!bytes) return -1;
        if (im->mime_type && im->mime_type[0]) mime = im->mime_type;
        else if (im->uri) mime = mime_of(im->uri);
    }

    strbuf uri;
    memset(&uri, 0, sizeof(uri));
    str_addf(&uri, "data:%s;base64,", mime);
    base64_encode(&uri, bytes, (size_t)size);
    free(bytes);

    int index = exp_img_add(st, uri.data ? uri.data : "");
    str_free(&uri);
    return index;
}

/* ------------------------------------------------------------- the export */

SDGE_API int sdge_editor_export(sdge_editor *ed, const char *path) {
    if (!ed || !path || !path[0]) return -1;
    sdge_model *model = sdge_editor_model(ed);
    if (!model) return -1;
    const sdge_gltf_model *g = sdge_model_gltf(model);
    if (!g) return -1;

    export_state st;
    memset(&st, 0, sizeof(st));
    st.ed = ed;
    st.g = g;
    dir_of(sdge_editor_path(ed), st.dir, sizeof(st.dir));

    int node_count = g->node_count > 0 ? g->node_count : 1;
    int tex_count = g->texture_count > 0 ? g->texture_count : 1;

    /* the map of the textures of the original file */
    int *tex_map = (int *)calloc((size_t)tex_count, sizeof(int));
    int *node_mesh = (int *)calloc((size_t)node_count, sizeof(int));
    int *mesh_of_gltf = (int *)calloc((size_t)(g->mesh_count > 0 ?
                                              g->mesh_count : 1),
                                      sizeof(int));
    if (!tex_map || !node_mesh || !mesh_of_gltf) {
        free(tex_map); free(node_mesh); free(mesh_of_gltf);
        return -1;
    }
    for (int i = 0; i < tex_count; ++i) tex_map[i] = -1;
    for (int i = 0; i < node_count; ++i) node_mesh[i] = -1;
    for (int i = 0; i < g->mesh_count; ++i) mesh_of_gltf[i] = -1;

    /* ---- the images and the textures of the original file ---- */
    for (int i = 0; i < g->texture_count; ++i) {
        const sdge_gltf_texture *tx = &g->textures[i];
        if (tx->source < 0 || tx->source >= g->image_count) continue;
        int img = export_image(&st, &g->images[tx->source], NULL);
        if (img < 0) continue;
        exp_texture t;
        t.image = img;
        int tex = exp_tex_add(&st, t);
        tex_map[i] = tex;
    }

    /* ---- the materials of the original file ---- */
    for (int i = 0; i < g->material_count; ++i) {
        const sdge_gltf_material *src = &g->materials[i];
        exp_material m;
        memset(&m, 0, sizeof(m));
        m.from_gltf = i;
        memcpy(m.base_color_f, src->base_color, sizeof(m.base_color_f));
        memcpy(m.emissive_f, src->emissive, sizeof(m.emissive_f));
        m.metallic = src->metallic;
        m.roughness = src->roughness;
        m.normal_scale = src->normal_tex.scale;
        m.occlusion_strength = src->occlusion_tex.scale;
        m.alpha_cutoff = src->alpha_cutoff;
        m.alpha_mode = src->alpha_mode;
        m.double_sided = src->double_sided;
        m.base_color = (src->base_color_tex.index >= 0 &&
                        src->base_color_tex.index < g->texture_count)
                       ? tex_map[src->base_color_tex.index] : -1;
        m.mr = (src->metallic_roughness_tex.index >= 0 &&
                src->metallic_roughness_tex.index < g->texture_count)
               ? tex_map[src->metallic_roughness_tex.index] : -1;
        m.normal = (src->normal_tex.index >= 0 &&
                    src->normal_tex.index < g->texture_count)
                   ? tex_map[src->normal_tex.index] : -1;
        m.occlusion = (src->occlusion_tex.index >= 0 &&
                      src->occlusion_tex.index < g->texture_count)
                     ? tex_map[src->occlusion_tex.index] : -1;
        m.emissive = (src->emissive_tex.index >= 0 &&
                      src->emissive_tex.index < g->texture_count)
                     ? tex_map[src->emissive_tex.index] : -1;
        exp_mat_add(&st, m);
    }

    /* ---- the meshes of the original file ---- */
    for (int i = 0; i < g->mesh_count; ++i) {
        const sdge_gltf_mesh *mesh = &g->meshes[i];
        exp_mesh em;
        memset(&em, 0, sizeof(em));
        em.gltf_mesh = i;
        em.node = -1;
        em.prim_count = mesh->prim_count;
        for (int p = 0; p < mesh->prim_count && p < 16; ++p) {
            int mat = mesh->prims[p].material;
            em.material[p] = (mat >= 0 && mat < g->material_count) ? mat : -1;
        }
        mesh_of_gltf[i] = exp_mesh_add(&st, em);
    }

    /* ---- the nodes with their own texture ---- */
    for (int node = 0; node < g->node_count; ++node) {
        int has_override = 0;
        for (int s = 0; s < SDGR_TEX_SLOTS; ++s) {
            const char *file = sdge_editor_texture_path(ed, node, s);
            if (file && file[0]) has_override = 1;
        }
        if (!has_override) continue;

        /* the new textures (the files are embedded) */
        int tex_index[SDGR_TEX_SLOTS];
        for (int s = 0; s < SDGR_TEX_SLOTS; ++s) tex_index[s] = -1;
        for (int s = 0; s < SDGR_TEX_SLOTS; ++s) {
            const char *file = sdge_editor_texture_path(ed, node, s);
            if (!file || !file[0]) continue;
            int img = export_image(&st, NULL, file);
            if (img < 0) continue;
            exp_texture t;
            t.image = img;
            tex_index[s] = exp_tex_add(&st, t);
        }

        /* the new material (a copy of the material of the node) */
        int mesh_index = g->nodes[node].mesh;
        int base_mat = -1;
        if (mesh_index >= 0 && mesh_index < g->mesh_count &&
            g->meshes[mesh_index].prim_count > 0)
            base_mat = g->meshes[mesh_index].prims[0].material;

        exp_material nm;
        memset(&nm, 0, sizeof(nm));
        nm.from_gltf = -1;
        if (base_mat >= 0 && base_mat < st.mat_count) {
            nm = st.mats[base_mat];
            nm.from_gltf = -1;      /* a copy */
        } else {
            v4_set(nm.base_color_f, 1, 1, 1, 1);
            nm.roughness = 0.6f;
            nm.alpha_cutoff = 0.5f;
        }
        for (int s = 0; s < SDGR_TEX_SLOTS; ++s) {
            if (tex_index[s] < 0) continue;
            switch (s) {
            case SDGE_EDIT_TEX_BASE: nm.base_color = tex_index[s]; break;
            case SDGE_EDIT_TEX_METALLIC: nm.mr = tex_index[s]; break;
            case SDGE_EDIT_TEX_NORMAL: nm.normal = tex_index[s]; break;
            case SDGE_EDIT_TEX_OCCLUSION: nm.occlusion = tex_index[s]; break;
            case SDGE_EDIT_TEX_EMISSIVE: nm.emissive = tex_index[s]; break;
            default: break;
            }
        }
        int new_mat = exp_mat_add(&st, nm);

        /* the copy of the mesh */
        if (mesh_index >= 0 && mesh_index < g->mesh_count) {
            exp_mesh em;
            memset(&em, 0, sizeof(em));
            em.gltf_mesh = mesh_index;
            em.node = node;
            em.prim_count = g->meshes[mesh_index].prim_count;
            for (int p = 0; p < em.prim_count && p < 16; ++p) {
                int mat = g->meshes[mesh_index].prims[p].material;
                em.material[p] = (mat == base_mat) ? new_mat
                           : ((mat >= 0 && mat < g->material_count) ? mat : -1);
            }
            node_mesh[node] = exp_mesh_add(&st, em);
        }
    }

    /* ---- the binary data of the meshes ---- */
    for (int i = 0; i < st.mesh_count; ++i) {
        int gi = st.meshes[i].gltf_mesh;
        const sdge_gltf_mesh *mesh = &g->meshes[gi];

        /* the copies of a mesh share the accessors of the original */
        if (mesh_of_gltf[gi] != i && mesh_of_gltf[gi] >= 0) {
            for (int k = 0; k < 4; ++k)
                st.meshes[i].acc[k] = st.meshes[mesh_of_gltf[gi]].acc[k];
            continue;
        }

        for (int p = 0; p < mesh->prim_count; ++p) {
            const sdge_primitive *prim = &mesh->prims[p];
            const sdge_accessor *pa = &prim->positions;
            if (pa->count <= 0) continue;

            /* the positions */
            float *verts = (float *)malloc(sizeof(float) * 3u *
                                           (size_t)pa->count);
            if (!verts) continue;
            float mn[3] = { 1e30f, 1e30f, 1e30f };
            float mx[3] = { -1e30f, -1e30f, -1e30f };
            for (int v = 0; v < pa->count; ++v) {
                for (int k = 0; k < 3; ++k) {
                    float f = acc_read(pa, v, k);
                    verts[v * 3 + k] = f;
                    if (f < mn[k]) mn[k] = f;
                    if (f > mx[k]) mx[k] = f;
                }
            }
            long off = bin_write(&st.bin, verts,
                                 sizeof(float) * 3u * (size_t)pa->count);
            free(verts);
            int view = exp_view_add(&st, off,
                                    (int)(sizeof(float) * 3u *
                                          (size_t)pa->count));
            exp_accessor a;
            memset(&a, 0, sizeof(a));
            a.count = pa->count;
            a.comps = 3;
            a.ctype = SDGE_GLTF_FLOAT;
            a.view = view;
            memcpy(a.min, mn, sizeof(mn));
            memcpy(a.max, mx, sizeof(mx));
            a.has_bounds = 1;
            st.meshes[i].acc[0] = exp_acc_add(&st, a);

            /* the normals */
            if (prim->normals.count > 0) {
                const sdge_accessor *na = &prim->normals;
                float *nrm = (float *)malloc(sizeof(float) * 3u *
                                            (size_t)na->count);
                if (nrm) {
                    for (int v = 0; v < na->count; ++v)
                        for (int k = 0; k < 3; ++k)
                            nrm[v * 3 + k] = acc_read(na, v, k);
                    long noff = bin_write(&st.bin, nrm,
                                          sizeof(float) * 3u *
                                          (size_t)na->count);
                    free(nrm);
                    int nview = exp_view_add(&st, noff,
                                             (int)(sizeof(float) * 3u *
                                                   (size_t)na->count));
                    exp_accessor na2;
                    memset(&na2, 0, sizeof(na2));
                    na2.count = na->count;
                    na2.comps = 3;
                    na2.ctype = SDGE_GLTF_FLOAT;
                    na2.view = nview;
                    st.meshes[i].acc[1] = exp_acc_add(&st, na2);
                }
            }
            /* the texture coordinates */
            if (prim->texcoords.count > 0) {
                const sdge_accessor *ta = &prim->texcoords;
                float *uv = (float *)malloc(sizeof(float) * 2u *
                                            (size_t)ta->count);
                if (uv) {
                    for (int v = 0; v < ta->count; ++v)
                        for (int k = 0; k < 2; ++k)
                            uv[v * 2 + k] = acc_read(ta, v, k);
                    long uoff = bin_write(&st.bin, uv,
                                          sizeof(float) * 2u *
                                          (size_t)ta->count);
                    free(uv);
                    int uview = exp_view_add(&st, uoff,
                                             (int)(sizeof(float) * 2u *
                                                   (size_t)ta->count));
                    exp_accessor ua;
                    memset(&ua, 0, sizeof(ua));
                    ua.count = ta->count;
                    ua.comps = 2;
                    ua.ctype = SDGE_GLTF_FLOAT;
                    ua.view = uview;
                    st.meshes[i].acc[2] = exp_acc_add(&st, ua);
                }
            }
            /* the indices */
            int index_count = prim->indices.count > 0 ? prim->indices.count
                                                      : pa->count;
            if (index_count <= 0) continue;
            unsigned *idx = (unsigned *)malloc(sizeof(unsigned) *
                                               (size_t)index_count);
            if (!idx) continue;
            for (int v = 0; v < index_count; ++v) {
                if (prim->indices.count > 0)
                    idx[v] = acc_read_index(&prim->indices, v);
                else
                    idx[v] = (unsigned)v;
            }
            long ioff = bin_write(&st.bin, idx,
                                  sizeof(unsigned) * (size_t)index_count);
            free(idx);
            int iview = exp_view_add(&st, ioff,
                                     (int)(sizeof(unsigned) *
                                           (size_t)index_count));
            exp_accessor ia;
            memset(&ia, 0, sizeof(ia));
            ia.count = index_count;
            ia.comps = 1;
            ia.ctype = SDGE_GLTF_UNSIGNED_INT;
            ia.view = iview;
            st.meshes[i].acc[3] = exp_acc_add(&st, ia);
        }
    }

    /* ---- the json ---- */
    strbuf *j = &st.json;
    str_add(j, "{\n");
    str_add(j, "\"asset\": {\"version\": \"2.0\", \"generator\": "
                "\"Silver Diamond Game Engine scene editor\"},\n");

    /* the scenes */
    str_add(j, "\"scenes\": [{\"nodes\": [");
    for (int i = 0; i < g->root_count; ++i)
        str_addf(j, "%s%d", i ? ", " : "", g->roots[i]);
    str_add(j, "]}],\n");
    str_add(j, "\"scene\": 0,\n");

    /* the nodes */
    str_add(j, "\"nodes\": [\n");
    for (int i = 0; i < g->node_count; ++i) {
        const sdge_gltf_node *n = &g->nodes[i];
        sdg_vec3 t, s;
        sdg_quat r;
        sdge_model_node_get_transform(model, i, t, r, s);
        str_add(j, "  {\"name\": ");
        str_add_json(j, n->name ? n->name : "");
        str_addf(j, ", \"translation\": [%.6f, %.6f, %.6f]",
                 t[0], t[1], t[2]);
        str_addf(j, ", \"rotation\": [%.6f, %.6f, %.6f, %.6f]",
                 r[0], r[1], r[2], r[3]);
        str_addf(j, ", \"scale\": [%.6f, %.6f, %.6f]", s[0], s[1], s[2]);
        int mesh = node_mesh[i] >= 0 ? node_mesh[i] : n->mesh;
        if (mesh >= 0) str_addf(j, ", \"mesh\": %d", mesh);
        if (n->skin >= 0 && n->skin < g->skin_count)
            str_addf(j, ", \"skin\": %d", n->skin);
        if (n->child_count > 0) {
            str_add(j, ", \"children\": [");
            for (int k = 0; k < n->child_count; ++k)
                str_addf(j, "%s%d", k ? ", " : "", n->children[k]);
            str_add(j, "]");
        }
        str_add(j, i + 1 < g->node_count ? "},\n" : "}\n");
    }
    str_add(j, "],\n");

    /* the meshes */
    str_add(j, "\"meshes\": [\n");
    for (int i = 0; i < st.mesh_count; ++i) {
        const sdge_gltf_mesh *mesh = &g->meshes[st.meshes[i].gltf_mesh];
        str_add(j, "  {\"name\": ");
        str_add_json(j, mesh->name ? mesh->name : "");
        str_add(j, ", \"primitives\": [");
        int first = 1;
        for (int p = 0; p < mesh->prim_count; ++p) {
            const sdge_primitive *prim = &mesh->prims[p];
            if (prim->positions.count <= 0) continue;
            if (!first) str_add(j, ", ");
            first = 0;
            str_addf(j, "{\"attributes\": {\"POSITION\": %d", st.meshes[i].acc[0]);
            if (st.meshes[i].acc[1] >= 0)
                str_addf(j, ", \"NORMAL\": %d", st.meshes[i].acc[1]);
            if (st.meshes[i].acc[2] >= 0)
                str_addf(j, ", \"TEXCOORD_0\": %d", st.meshes[i].acc[2]);
            str_add(j, "}");
            if (st.meshes[i].acc[3] >= 0)
                str_addf(j, ", \"indices\": %d", st.meshes[i].acc[3]);
            if (st.meshes[i].material[p] >= 0)
                str_addf(j, ", \"material\": %d", st.meshes[i].material[p]);
            str_add(j, "}");
        }
        str_add(j, i + 1 < st.mesh_count ? "]},\n" : "]}\n");
    }
    str_add(j, "],\n");

    /* the materials */
    str_add(j, "\"materials\": [\n");
    for (int i = 0; i < st.mat_count; ++i) {
        const exp_material *m = &st.mats[i];
        str_add(j, "  {\"name\": ");
        if (m->from_gltf >= 0 && m->from_gltf < g->material_count)
            str_add_json(j, g->materials[m->from_gltf].name ?
                            g->materials[m->from_gltf].name : "");
        else
            str_add_json(j, "edited");
        str_addf(j, ", \"pbrMetallicRoughness\": {\"baseColorFactor\": "
                     "[%.4f, %.4f, %.4f, %.4f], \"metallicFactor\": %.4f, "
                     "\"roughnessFactor\": %.4f",
                 m->base_color_f[0], m->base_color_f[1], m->base_color_f[2],
                 m->base_color_f[3], m->metallic, m->roughness);
        if (m->base_color >= 0)
            str_addf(j, ", \"baseColorTexture\": {\"index\": %d}",
                     m->base_color);
        if (m->mr >= 0)
            str_addf(j, ", \"metallicRoughnessTexture\": {\"index\": %d}",
                     m->mr);
        str_add(j, "}");
        if (m->normal >= 0)
            str_addf(j, ", \"normalTexture\": {\"index\": %d, \"scale\": %.4f}",
                     m->normal, m->normal_scale);
        if (m->occlusion >= 0)
            str_addf(j, ", \"occlusionTexture\": {\"index\": %d, "
                     "\"strength\": %.4f}", m->occlusion, m->occlusion_strength);
        if (m->emissive >= 0)
            str_addf(j, ", \"emissiveTexture\": {\"index\": %d}", m->emissive);
        str_addf(j, ", \"emissiveFactor\": [%.4f, %.4f, %.4f]",
                 m->emissive_f[0], m->emissive_f[1], m->emissive_f[2]);
        str_addf(j, ", \"alphaMode\": \"%s\"",
                 m->alpha_mode == SDGE_ALPHA_MASK ? "MASK" :
                 (m->alpha_mode == SDGE_ALPHA_BLEND ? "BLEND" : "OPAQUE"));
        str_addf(j, ", \"alphaCutoff\": %.4f", m->alpha_cutoff);
        if (m->double_sided) str_add(j, ", \"doubleSided\": true");
        str_add(j, i + 1 < st.mat_count ? "},\n" : "}\n");
    }
    str_add(j, "],\n");

    /* the textures */
    str_add(j, "\"textures\": [\n");
    for (int i = 0; i < st.tex_count; ++i) {
        str_addf(j, "  {\"source\": %d, \"sampler\": 0}", st.texs[i].image);
        str_add(j, i + 1 < st.tex_count ? ",\n" : "\n");
    }
    str_add(j, "],\n");

    /* the images (the data URIs) */
    str_add(j, "\"images\": [\n");
    for (int i = 0; i < st.img_count; ++i) {
        str_add(j, "  {\"uri\": ");
        str_add_json(j, st.img_uris[i]);
        str_add(j, i + 1 < st.img_count ? "},\n" : "}\n");
    }
    str_add(j, "],\n");

    str_add(j, "\"samplers\": [{\"magFilter\": 9729, \"minFilter\": 9987, "
                "\"wrapS\": 10497, \"wrapT\": 10497}],\n");

    /* the accessors */
    str_add(j, "\"accessors\": [\n");
    for (int i = 0; i < st.acc_count; ++i) {
        const exp_accessor *a = &st.accs[i];
        str_addf(j, "  {\"bufferView\": %d, \"componentType\": %d, \"count\": %d",
                 a->view, a->ctype, a->count);
        if (a->comps == 1) str_add(j, ", \"type\": \"SCALAR\"");
        else if (a->comps == 2) str_add(j, ", \"type\": \"VEC2\"");
        else if (a->comps == 3) str_add(j, ", \"type\": \"VEC3\"");
        else str_add(j, ", \"type\": \"VEC4\"");
        if (a->has_bounds)
            str_addf(j, ", \"min\": [%.6f, %.6f, %.6f], "
                         "\"max\": [%.6f, %.6f, %.6f]",
                     a->min[0], a->min[1], a->min[2],
                     a->max[0], a->max[1], a->max[2]);
        str_add(j, i + 1 < st.acc_count ? "},\n" : "}\n");
    }
    str_add(j, "],\n");

    /* the buffer views */
    str_add(j, "\"bufferViews\": [\n");
    for (int i = 0; i < st.view_count; ++i) {
        str_addf(j, "  {\"buffer\": 0, \"byteOffset\": %d, \"byteLength\": %d}",
                 st.views[i], st.view_lens[i]);
        str_add(j, i + 1 < st.view_count ? ",\n" : "\n");
    }
    str_add(j, "],\n");

    /* the buffer: the uri is only the file name (the loader resolves it
     * against the directory of the gltf file) */
    {
        const char *base = path;
        for (const char *p = path; *p; ++p) {
            if (*p == '/' || *p == '\\') base = p + 1;
        }
        char bin_name[512];
        snprintf(bin_name, sizeof(bin_name), "%s.bin", base);
        str_addf(j, "\"buffers\": [{\"uri\": \"%s\", \"byteLength\": %d}]\n",
                 bin_name, (int)st.bin.size);
    }
    str_add(j, "}\n");

    /* ---- the files ---- */
    char bin_path[1024];
    snprintf(bin_path, sizeof(bin_path), "%s.bin", path);

    FILE *f = fopen(bin_path, "wb");
    if (!f) {
        fprintf(stderr, "editor: cannot write '%s'\n", bin_path);
        return -1;
    }
    if (st.bin.size) fwrite(st.bin.data, 1, st.bin.size, f);
    fclose(f);

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "editor: cannot write '%s'\n", path);
        return -1;
    }
    if (j->data) fwrite(j->data, 1, j->size, f);
    fclose(f);

    fprintf(stdout, "editor: saved '%s' (+ %s, %d bytes)\n", path, bin_path,
            (int)st.bin.size);

    /* the clean up */
    for (int i = 0; i < st.img_count; ++i) free(st.img_uris[i]);
    free(st.img_uris);
    free(tex_map);
    free(node_mesh);
    free(mesh_of_gltf);
    free(st.accs);
    free(st.views);
    free(st.view_lens);
    free(st.meshes);
    free(st.mats);
    free(st.texs);
    bin_free(&st.bin);
    str_free(j);
    return 0;
}
