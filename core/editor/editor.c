/* editor.c - Silver Diamond Game Engine :: scene editor implementation
 *
 * The editor owns a scene: a list of objects.  Every object is empty or
 * holds one glTF model; the nodes of the model are the children in the
 * hierarchy.  The objects can be moved, turned and scaled as a whole while
 * every glTF node can still be edited on its own.  The complete scene is
 * saved as a .sdscene file (JSON), so it is a real scene format and not
 * just one exported model.
 */
#include "editor.h"

#include "../assets/assets.h"
#include "../assets/json.h"
#include "../assets/texture.h"
#include "../graphics/renderer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#ifdef _WIN32
    #include <direct.h>
    #define ED_MKDIR(p) _mkdir(p)
#else
    #include <sys/stat.h>
    #include <sys/types.h>
    #define ED_MKDIR(p) mkdir((p), 0755)
#endif

#define EDIT_MAX_OBJECTS 128
#define EDIT_MAX_ITEMS   8192
#define EDIT_PATH_MAX    512

/* the colours of the gizmo */
static const float COL_AXIS_X[4] = { 0.90f, 0.25f, 0.28f, 1.0f };
static const float COL_AXIS_Y[4] = { 0.35f, 0.85f, 0.35f, 1.0f };
static const float COL_AXIS_Z[4] = { 0.30f, 0.50f, 0.95f, 1.0f };
static const float COL_SELECT[4] = { 1.00f, 0.85f, 0.25f, 1.0f };

/* one object of the scene */
typedef struct {
    char       name[96];
    char       path[EDIT_PATH_MAX];
    sdge_model *model;
    sdg_vec3   pos;
    sdg_quat   rot;
    sdg_vec3   scale;
    int        visible;
    int        expanded;
} ed_object;

/* the file of every texture that was put on a node (the export needs it to
 * embed the image into the new glTF file) */
typedef struct {
    int  object;
    int  node;
    int  slot;
    char path[EDIT_PATH_MAX];
} ed_tex_ref;

struct sdge_editor {
    ed_object objects[EDIT_MAX_OBJECTS];
    int object_count;
    int active;

    char scene_name[96];
    char scene_path[EDIT_PATH_MAX];
    int  scene_dirty;

    sdge_edit_item items[EDIT_MAX_ITEMS];
    int item_count;
    int selected;

    int tool;
    int axis;
    int play_state;
    int grid_visible;

    sdge_camera3d cam;
    float focus_center[3];
    float focus_radius;

    int vp_x, vp_y, vp_w, vp_h;

    /* the last mouse position inside the viewport */
    float last_mx, last_my;

    /* the drag */
    int    dragging;
    int    drag_item;
    int    drag_tool;
    float  drag_mx, drag_my;
    float  drag_origin[3];      /* the origin of the item at the start */
    sdg_quat drag_rot;
    sdg_vec3 drag_scale;
    sdg_vec3 drag_translation;
    float  drag_plane[4];       /* the plane of the move (n, d) */

    char   error[256];

    /* the files of the textures that were put on the nodes */
    ed_tex_ref *tex_refs;
    int    tex_ref_count;
    int    tex_ref_cap;
};

/* the ray against an axis aligned box (the slab method) */
static int ray_box(const float o[3], const float d[3], const float lo[3],
                   const float hi[3], float *t0_out, float *t1_out);

/* --------------------------------------------------------------- helpers */

static void ed_error(sdge_editor *ed, const char *msg) {
    if (!ed) return;
    snprintf(ed->error, sizeof(ed->error), "%s", msg ? msg : "");
}

static void ed_clear_error(sdge_editor *ed) {
    if (ed) ed->error[0] = '\0';
}

static void ed_dirty(sdge_editor *ed) {
    if (ed) ed->scene_dirty = 1;
}

static void ed_object_matrix(const ed_object *o, sdg_mat4 out) {
    m4_from_trs(out, o->pos, o->rot, o->scale);
}

static ed_object *ed_get_object(sdge_editor *ed, int object) {
    if (!ed || object < 0 || object >= ed->object_count) return NULL;
    return &ed->objects[object];
}

static const ed_object *ed_get_object_c(const sdge_editor *ed, int object) {
    if (!ed || object < 0 || object >= ed->object_count) return NULL;
    return &ed->objects[object];
}

static const char *ed_base_name(const char *path) {
    const char *p = path ? path : "";
    const char *slash = strrchr(p, '/');
    const char *bslash = strrchr(p, '\\');
    if (bslash && bslash > slash) slash = bslash;
    if (slash) p = slash + 1;
    return p;
}

static void ed_name_from_path(const char *path, char *out, size_t outn) {
    const char *base = ed_base_name(path);
    snprintf(out, outn, "%s", base && base[0] ? base : "Object");
    char *dot = strrchr(out, '.');
    if (dot && dot != out) *dot = '\0';
}

static void ed_dir_of(const char *path, char *out, size_t outn) {
    out[0] = '\0';
    if (!path) return;
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && bslash > slash) slash = bslash;
    if (!slash) return;
    size_t n = (size_t)(slash - path);
    if (n >= outn) n = outn - 1;
    memcpy(out, path, n);
    out[n] = '\0';
}

/* creates every missing directory of a path, not just the last one */
static void ed_make_dirs(const char *path) {
    char dir[EDIT_PATH_MAX];
    ed_dir_of(path, dir, sizeof(dir));
    if (!dir[0]) return;
    for (char *p = dir + 1; *p; ++p) {
        if (*p != '/' && *p != '\\') continue;
        char sep = *p;
        *p = '\0';
        ED_MKDIR(dir);
        *p = sep;
    }
    ED_MKDIR(dir);
}

/* the ray of the mouse through the viewport */
static void ed_ray(sdge_editor *ed, float mx, float my,
                   float origin[3], float dir[3]) {
    v3_set(origin, 0, 0, 0);
    v3_set(dir, 0, 0, -1);
    if (ed->vp_w <= 0 || ed->vp_h <= 0) return;

    float ndc_x = (mx / (float)ed->vp_w) * 2.0f - 1.0f;
    float ndc_y = 1.0f - (my / (float)ed->vp_h) * 2.0f;

    sdg_mat4 inv;
    m4_invert(inv, ed->cam.vp);
    sdg_vec4 n = { ndc_x, ndc_y, -1.0f, 1.0f };
    sdg_vec4 f = { ndc_x, ndc_y,  1.0f, 1.0f };
    sdg_vec4 n4, f4;
    m4_mul_vec4(n4, inv, n);
    m4_mul_vec4(f4, inv, f);
    if (fabsf(n4[3]) < 1e-9f || fabsf(f4[3]) < 1e-9f) return;
    float np[3] = { n4[0] / n4[3], n4[1] / n4[3], n4[2] / n4[3] };
    float fp[3] = { f4[0] / f4[3], f4[1] / f4[3], f4[2] / f4[3] };
    v3_copy(origin, np);
    v3_sub(dir, fp, np);
    v3_norm(dir, dir);
}

/* the ray against a triangle (Moeller-Trumbore) */
static int ray_triangle(const float o[3], const float d[3],
                        const float a[3], const float b[3], const float c[3],
                        float *t_out) {
    float e1[3], e2[3], p[3], q[3], t[3], dv[3];
    v3_sub(e1, b, a);
    v3_sub(e2, c, a);
    v3_copy(dv, d);
    v3_cross(p, dv, e2);
    float det = v3_dot(e1, p);
    if (fabsf(det) < 1e-10f) return 0;
    float inv = 1.0f / det;
    v3_sub(t, o, a);
    float u = v3_dot(t, p) * inv;
    if (u < 0.0f || u > 1.0f) return 0;
    v3_cross(q, t, e1);
    float v = v3_dot(dv, q) * inv;
    if (v < 0.0f || u + v > 1.0f) return 0;
    float tt = v3_dot(e2, q) * inv;
    if (tt < 0.0f) return 0;
    if (t_out) *t_out = tt;
    return 1;
}

/* the intersection of a ray with a plane (n, d): n.x + d = 0 */
static int ray_plane(const float o[3], const float d[3], const float n[3],
                     float plane_d, float out[3]) {
    float denom = v3_dot(d, n);
    if (fabsf(denom) < 1e-8f) return 0;
    float t = -(v3_dot(o, n) + plane_d) / denom;
    out[0] = o[0] + d[0] * t;
    out[1] = o[1] + d[1] * t;
    out[2] = o[2] + d[2] * t;
    return 1;
}

/* reads one component of an accessor (the glTF component types) */
static float acc_get(const sdge_accessor *a, int index, int comp) {
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

static int acc_index(const sdge_accessor *a, int i) {
    if (!a || !a->data || i < 0 || i >= a->count) return 0;
    const unsigned char *p = a->data + (size_t)i * (size_t)a->stride;
    switch (a->ctype) {
    case SDGE_GLTF_UNSIGNED_INT: {
        unsigned int v; memcpy(&v, p, 4); return (int)v;
    }
    case SDGE_GLTF_UNSIGNED_SHORT: {
        unsigned short v; memcpy(&v, p, 2); return (int)v;
    }
    case SDGE_GLTF_UNSIGNED_BYTE: return (int)p[0];
    default: break;
    }
    return 0;
}

/* --------------------------------------------------------- the hierarchy */

static int ed_find_item(sdge_editor *ed, int object, int node) {
    for (int i = 0; i < ed->item_count; ++i) {
        if (ed->items[i].object == object && ed->items[i].node == node)
            return i;
    }
    return -1;
}

static void ed_add_node_tree(sdge_editor *ed, int object, int node, int depth) {
    if (!ed || node < 0 || ed->item_count >= EDIT_MAX_ITEMS) return;
    ed_object *o = ed_get_object(ed, object);
    if (!o || !o->model) return;

    const sdge_gltf_model *g = sdge_model_gltf(o->model);
    if (!g || node >= g->node_count) return;

    sdge_edit_item *it = &ed->items[ed->item_count++];
    it->object = object;
    it->node = node;
    it->depth = depth;
    it->has_mesh = (g->nodes[node].mesh >= 0);
    const char *name = g->nodes[node].name;
    if (!name || !name[0]) {
        if (it->has_mesh)
            snprintf(it->name, sizeof(it->name), "mesh %d", g->nodes[node].mesh);
        else
            snprintf(it->name, sizeof(it->name), "node %d", node);
    } else {
        snprintf(it->name, sizeof(it->name), "%s", name);
    }

    int kids[256];
    int count = sdge_model_node_children(o->model, node, kids, 256);
    for (int i = 0; i < count; ++i)
        ed_add_node_tree(ed, object, kids[i], depth + 1);
}

static void ed_build_items(sdge_editor *ed) {
    int sel_object = -1, sel_node = -1;
    if (ed->selected >= 0 && ed->selected < ed->item_count) {
        sel_object = ed->items[ed->selected].object;
        sel_node = ed->items[ed->selected].node;
    }

    ed->item_count = 0;
    for (int obj = 0; obj < ed->object_count; ++obj) {
        ed_object *o = &ed->objects[obj];
        if (ed->item_count >= EDIT_MAX_ITEMS) break;
        sdge_edit_item *it = &ed->items[ed->item_count++];
        it->object = obj;
        it->node = -1;
        it->depth = 0;
        it->has_mesh = o->model ? 1 : 0;
        snprintf(it->name, sizeof(it->name), "%s", o->name);

        if (!o->model || !o->expanded) continue;

        const sdge_gltf_model *g = sdge_model_gltf(o->model);
        if (!g) continue;
        if (g->root_count > 0) {
            for (int i = 0; i < g->root_count; ++i)
                ed_add_node_tree(ed, obj, g->roots[i], 1);
        } else {
            for (int n = 0; n < g->node_count; ++n) {
                if (sdge_model_node_parent(o->model, n) < 0)
                    ed_add_node_tree(ed, obj, n, 1);
            }
        }
    }

    ed->selected = -1;
    if (sel_object >= 0) {
        int found = ed_find_item(ed, sel_object, sel_node);
        if (found >= 0) ed->selected = found;
    }
}

static void ed_destroy_object_model(ed_object *o) {
    if (!o) return;
    if (o->model) sdge_model_destroy(o->model);
    o->model = NULL;
}

/* --------------------------------------------------------- object helper */

static int ed_add_object_with_model(sdge_editor *ed, const char *path,
                                     const char *name, sdge_model *model) {
    if (!ed) return -1;
    if (ed->object_count >= EDIT_MAX_OBJECTS) {
        ed_error(ed, "the scene has too many objects");
        return -1;
    }
    ed_object *o = &ed->objects[ed->object_count];
    memset(o, 0, sizeof(*o));
    if (name && name[0]) snprintf(o->name, sizeof(o->name), "%s", name);
    else if (path && path[0]) ed_name_from_path(path, o->name, sizeof(o->name));
    else snprintf(o->name, sizeof(o->name), "GameObject");
    if (path) snprintf(o->path, sizeof(o->path), "%s", path);
    o->model = model;
    o->visible = 1;
    o->expanded = 1;
    v3_set(o->pos, 0.0f, 0.0f, 0.0f);
    q_identity(o->rot);
    v3_set(o->scale, 1.0f, 1.0f, 1.0f);
    int index = ed->object_count++;
    ed_build_items(ed);
    return index;
}

static sdge_model *ed_load_model(const char *path, const char *base_dir) {
    if (!path || !path[0]) return NULL;
    sdge_model *m = sdge_model_load(path);
    if (!m && base_dir && base_dir[0]) {
        char alt[EDIT_PATH_MAX];
        snprintf(alt, sizeof(alt), "%s/%s", base_dir, path);
        m = sdge_model_load(alt);
    }
    return m;
}

/* uploads a texture and puts it on a node of an object (used by the scene
 * loader and by duplication); 0 = ok */
static int ed_apply_texture_node(sdge_editor *ed, int object, int node,
                                  const char *path, int slot) {
    if (!ed) return -1;
    ed_object *o = ed_get_object(ed, object);
    if (!o || !o->model || !path || !path[0]) return -1;
    if (node < 0 || slot < 0 || slot >= SDGR_TEX_SLOTS) return -1;

    sdga_image img;
    if (sdga_image_load_rgba(path, &img) != 0) {
        /* the scene still keeps the path, so a missing image is not fatal */
        return -1;
    }
    sdgr_texture_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.srgb = (slot == SDGE_EDIT_TEX_BASE || slot == SDGE_EDIT_TEX_EMISSIVE);
    desc.wrap_s = desc.wrap_t = SDGR_REPEAT;
    desc.min_filter = SDGR_LINEAR_MIPMAP_LINEAR;
    desc.mag_filter = SDGR_LINEAR;
    desc.mipmaps = 1;
    unsigned tex = sdgr_upload_texture_ex(img.w, img.h, img.pixels, &desc);
    sdga_image_free(&img);
    if (!tex) return -1;
    if (sdge_model_set_node_texture(o->model, node, slot, tex) != 0) {
        sdgr_free_texture(tex);
        return -1;
    }
    return 0;
}

static const char *ed_tex_find(sdge_editor *ed, int object, int node,
                                int slot) {
    if (!ed) return NULL;
    for (int i = 0; i < ed->tex_ref_count; ++i) {
        if (ed->tex_refs[i].object == object &&
            ed->tex_refs[i].node == node &&
            ed->tex_refs[i].slot == slot)
            return ed->tex_refs[i].path;
    }
    return NULL;
}

static void ed_tex_set(sdge_editor *ed, int object, int node, int slot,
                       const char *path) {
    if (!ed) return;
    for (int i = 0; i < ed->tex_ref_count; ++i) {
        if (ed->tex_refs[i].object == object &&
            ed->tex_refs[i].node == node &&
            ed->tex_refs[i].slot == slot) {
            if (!path) {
                ed->tex_refs[i] = ed->tex_refs[ed->tex_ref_count - 1];
                ed->tex_ref_count--;
            } else {
                snprintf(ed->tex_refs[i].path, sizeof(ed->tex_refs[i].path),
                         "%s", path);
            }
            return;
        }
    }
    if (!path) return;
    if (ed->tex_ref_count >= ed->tex_ref_cap) {
        int cap = ed->tex_ref_cap ? ed->tex_ref_cap * 2 : 8;
        ed_tex_ref *n = (ed_tex_ref *)realloc(ed->tex_refs,
                                              sizeof(ed_tex_ref) * (size_t)cap);
        if (!n) return;
        ed->tex_refs = n;
        ed->tex_ref_cap = cap;
    }
    ed_tex_ref *r = &ed->tex_refs[ed->tex_ref_count++];
    r->object = object;
    r->node = node;
    r->slot = slot;
    snprintf(r->path, sizeof(r->path), "%s", path);
}

static void ed_tex_remove_object(sdge_editor *ed, int object) {
    if (!ed) return;
    for (int i = ed->tex_ref_count - 1; i >= 0; --i) {
        if (ed->tex_refs[i].object == object) {
            ed->tex_refs[i] = ed->tex_refs[ed->tex_ref_count - 1];
            ed->tex_ref_count--;
        } else if (ed->tex_refs[i].object > object) {
            ed->tex_refs[i].object--;
        }
    }
}

static void ed_copy_node_transforms(sdge_editor *ed, int src, int dst) {
    ed_object *a = ed_get_object(ed, src);
    ed_object *b = ed_get_object(ed, dst);
    if (!a || !b || !a->model || !b->model) return;
    int count = sdge_model_node_count(a->model);
    int dst_count = sdge_model_node_count(b->model);
    if (count > dst_count) count = dst_count;
    for (int i = 0; i < count; ++i) {
        sdg_vec3 t, s;
        sdg_quat r;
        sdge_model_node_get_transform(a->model, i, t, r, s);
        sdge_model_node_set_transform(b->model, i, t, r, s);
    }
    sdge_model_refresh(b->model);
}

/* --------------------------------------------------------- the life cycle */

SDGE_API sdge_editor *sdge_editor_create(void) {
    sdge_editor *ed = (sdge_editor *)calloc(1, sizeof(*ed));
    if (!ed) return NULL;
    ed->selected = -1;
    ed->active = -1;
    ed->tool = SDGE_EDIT_SELECT;
    ed->axis = SDGE_EDIT_AXIS_NONE;
    ed->play_state = SDGE_EDIT_PLAY_STOPPED;
    ed->grid_visible = 1;
    ed->vp_w = 1;
    ed->vp_h = 1;
    sdge_camera3d_init(&ed->cam, 45.0f, 1.0f, 0.05f, 1000.0f);
    ed->cam.dist = 6.0f;
    ed->cam.yaw = 0.7f;
    ed->cam.pitch = 0.25f;
    snprintf(ed->scene_name, sizeof(ed->scene_name), "Untitled Scene");
    return ed;
}

SDGE_API void sdge_editor_destroy(sdge_editor *ed) {
    if (!ed) return;
    sdge_editor_new_scene(ed, "Untitled Scene");
    free(ed->tex_refs);
    free(ed);
}

SDGE_API void sdge_editor_new_scene(sdge_editor *ed, const char *name) {
    if (!ed) return;
    ed->play_state = SDGE_EDIT_PLAY_STOPPED;
    for (int i = 0; i < ed->object_count; ++i)
        ed_destroy_object_model(&ed->objects[i]);
    memset(ed->objects, 0, sizeof(ed->objects));
    ed->object_count = 0;
    ed->active = -1;
    ed->item_count = 0;
    ed->selected = -1;
    ed->tex_ref_count = 0;
    ed->scene_path[0] = '\0';
    ed->scene_dirty = 0;
    snprintf(ed->scene_name, sizeof(ed->scene_name), "%s",
             (name && name[0]) ? name : "Untitled Scene");
}

SDGE_API void sdge_editor_close(sdge_editor *ed) {
    sdge_editor_new_scene(ed, "Untitled Scene");
}

SDGE_API int sdge_editor_open(sdge_editor *ed, const char *path) {
    if (!ed) return -1;
    ed_clear_error(ed);
    if (!path || !path[0]) {
        ed_error(ed, "no file name");
        return -1;
    }
    sdge_editor_new_scene(ed, "Untitled Scene");
    ed_name_from_path(path, ed->scene_name, sizeof(ed->scene_name));
    int obj = sdge_editor_add_object(ed, path, ed->scene_name);
    if (obj < 0) {
        ed_error(ed, "cannot load the model");
        return -1;
    }
    ed->scene_dirty = 0;
    sdge_editor_focus(ed);
    return 0;
}

SDGE_API const char *sdge_editor_path(const sdge_editor *ed) {
    const ed_object *o = ed_get_object_c(ed, ed ? ed->active : -1);
    return o ? o->path : "";
}

SDGE_API sdge_model *sdge_editor_model(const sdge_editor *ed) {
    ed_object *o = ed_get_object((sdge_editor *)ed, ed ? ed->active : -1);
    return o ? o->model : NULL;
}

SDGE_API sdge_model *sdge_editor_object_model(const sdge_editor *ed,
                                              int object) {
    ed_object *o = ed_get_object((sdge_editor *)ed, object);
    return o ? o->model : NULL;
}

SDGE_API const char *sdge_editor_error(const sdge_editor *ed) {
    return ed ? ed->error : "";
}

/* --------------------------------------------------------- the hierarchy */

SDGE_API int sdge_editor_item_count(const sdge_editor *ed) {
    return ed ? ed->item_count : 0;
}

SDGE_API int sdge_editor_item_object(const sdge_editor *ed, int item) {
    if (!ed || item < 0 || item >= ed->item_count) return -1;
    return ed->items[item].object;
}

SDGE_API int sdge_editor_item_node(const sdge_editor *ed, int item) {
    if (!ed || item < 0 || item >= ed->item_count) return -1;
    return ed->items[item].node;
}

SDGE_API const char *sdge_editor_item_name(const sdge_editor *ed, int item) {
    if (!ed || item < 0 || item >= ed->item_count) return "";
    return ed->items[item].name;
}

SDGE_API int sdge_editor_item_depth(const sdge_editor *ed, int item) {
    if (!ed || item < 0 || item >= ed->item_count) return 0;
    return ed->items[item].depth;
}

SDGE_API int sdge_editor_item_has_mesh(const sdge_editor *ed, int item) {
    if (!ed || item < 0 || item >= ed->item_count) return 0;
    return ed->items[item].has_mesh;
}

/* ------------------------------------------------------- the scene objects */

SDGE_API int sdge_editor_object_count(const sdge_editor *ed) {
    return ed ? ed->object_count : 0;
}

SDGE_API int sdge_editor_add_object(sdge_editor *ed, const char *path,
                                     const char *name) {
    if (!ed) return -1;
    ed_clear_error(ed);
    sdge_model *model = NULL;
    if (path && path[0]) {
        model = ed_load_model(path, NULL);
        if (!model) {
            ed_error(ed, "cannot load the model");
            return -1;
        }
    }
    int index = ed_add_object_with_model(ed, path, name, model);
    if (index < 0) {
        if (model) sdge_model_destroy(model);
        return -1;
    }
    ed->active = index;
    ed_dirty(ed);
    sdge_editor_select_object(ed, index);
    return index;
}

SDGE_API int sdge_editor_add_empty(sdge_editor *ed, const char *name) {
    if (!ed) return -1;
    ed_clear_error(ed);
    int index = ed_add_object_with_model(ed, NULL, name, NULL);
    if (index < 0) return -1;
    ed->active = index;
    ed_dirty(ed);
    sdge_editor_select_object(ed, index);
    return index;
}

SDGE_API int sdge_editor_remove_object(sdge_editor *ed, int object) {
    if (!ed || object < 0 || object >= ed->object_count) return -1;
    ed_tex_remove_object(ed, object);
    ed_destroy_object_model(&ed->objects[object]);
    for (int i = object; i < ed->object_count - 1; ++i)
        ed->objects[i] = ed->objects[i + 1];
    ed->object_count--;
    memset(&ed->objects[ed->object_count], 0, sizeof(ed_object));

    if (ed->active == object) ed->active = ed->object_count > 0 ?
        (object < ed->object_count ? object : ed->object_count - 1) : -1;
    else if (ed->active > object) ed->active--;

    ed_build_items(ed);
    ed->selected = -1;
    if (ed->active >= 0) sdge_editor_select_object(ed, ed->active);
    ed_dirty(ed);
    return 0;
}

SDGE_API int sdge_editor_duplicate_object(sdge_editor *ed, int object) {
    if (!ed || object < 0 || object >= ed->object_count) return -1;
    ed_object *src = &ed->objects[object];
    if (ed->object_count >= EDIT_MAX_OBJECTS) return -1;

    sdge_model *model = NULL;
    if (src->path[0]) {
        model = ed_load_model(src->path, NULL);
        if (!model) return -1;
    }
    char name[96];
    snprintf(name, sizeof(name), "%s (Copy)", src->name);
    int index = ed_add_object_with_model(ed, src->path, name, model);
    if (index < 0) {
        if (model) sdge_model_destroy(model);
        return -1;
    }
    ed_object *dst = &ed->objects[index];
    v3_copy(dst->pos, src->pos);
    q_copy(dst->rot, src->rot);
    v3_copy(dst->scale, src->scale);
    dst->visible = src->visible;
    dst->expanded = src->expanded;

    ed_copy_node_transforms(ed, object, index);
    for (int i = 0; i < ed->tex_ref_count; ++i) {
        ed_tex_ref *r = &ed->tex_refs[i];
        if (r->object != object) continue;
        if (ed_apply_texture_node(ed, index, r->node, r->path, r->slot) == 0)
            ed_tex_set(ed, index, r->node, r->slot, r->path);
    }

    ed->active = index;
    ed_build_items(ed);
    sdge_editor_select_object(ed, index);
    ed_dirty(ed);
    return index;
}

SDGE_API int sdge_editor_active_object(const sdge_editor *ed) {
    return ed ? ed->active : -1;
}

SDGE_API void sdge_editor_set_active_object(sdge_editor *ed, int object) {
    if (!ed) return;
    if (object < -1 || object >= ed->object_count) return;
    ed->active = object;
}

SDGE_API int sdge_editor_select_object(sdge_editor *ed, int object) {
    if (!ed) return -1;
    if (object < 0 || object >= ed->object_count) {
        ed->selected = -1;
        ed->active = -1;
        return -1;
    }
    int item = ed_find_item(ed, object, -1);
    ed->selected = item;
    ed->active = object;
    return item;
}

SDGE_API const char *sdge_editor_object_name(const sdge_editor *ed, int object) {
    const ed_object *o = ed_get_object_c(ed, object);
    return o ? o->name : "";
}

SDGE_API void sdge_editor_set_object_name(sdge_editor *ed, int object,
                                          const char *name) {
    ed_object *o = ed_get_object(ed, object);
    if (!o || !name) return;
    snprintf(o->name, sizeof(o->name), "%s", name);
    ed_build_items(ed);
    ed_dirty(ed);
}

SDGE_API const char *sdge_editor_object_path(const sdge_editor *ed, int object) {
    const ed_object *o = ed_get_object_c(ed, object);
    return o ? o->path : "";
}

SDGE_API int sdge_editor_object_visible(const sdge_editor *ed, int object) {
    const ed_object *o = ed_get_object_c(ed, object);
    return o ? o->visible : 0;
}

SDGE_API void sdge_editor_set_object_visible(sdge_editor *ed, int object,
                                             int visible) {
    ed_object *o = ed_get_object(ed, object);
    if (!o) return;
    o->visible = visible ? 1 : 0;
    ed_dirty(ed);
}

SDGE_API int sdge_editor_object_expanded(const sdge_editor *ed, int object) {
    const ed_object *o = ed_get_object_c(ed, object);
    return o ? o->expanded : 0;
}

SDGE_API void sdge_editor_set_object_expanded(sdge_editor *ed, int object,
                                              int expanded) {
    ed_object *o = ed_get_object(ed, object);
    if (!o) return;
    o->expanded = expanded ? 1 : 0;
    ed_build_items(ed);
}

SDGE_API int sdge_editor_get_object_transform(sdge_editor *ed, int object,
                                              sdg_vec3 t, sdg_quat r,
                                              sdg_vec3 s) {
    ed_object *o = ed_get_object(ed, object);
    if (!o) return -1;
    if (t) v3_copy(t, o->pos);
    if (r) q_copy(r, o->rot);
    if (s) v3_copy(s, o->scale);
    return 0;
}

SDGE_API int sdge_editor_set_object_transform(sdge_editor *ed, int object,
                                              const sdg_vec3 t,
                                              const sdg_quat r,
                                              const sdg_vec3 s) {
    ed_object *o = ed_get_object(ed, object);
    if (!o) return -1;
    if (t) v3_copy(o->pos, t);
    if (r) q_copy(o->rot, r);
    if (s) v3_copy(o->scale, s);
    ed_dirty(ed);
    return 0;
}

/* ---------------------------------------------------------- the selection */

SDGE_API void sdge_editor_select(sdge_editor *ed, int item) {
    if (!ed) return;
    if (item < 0 || item >= ed->item_count) ed->selected = -1;
    else {
        ed->selected = item;
        ed->active = ed->items[item].object;
    }
}

SDGE_API int sdge_editor_selected(const sdge_editor *ed) {
    return ed ? ed->selected : -1;
}

SDGE_API int sdge_editor_selected_node(const sdge_editor *ed) {
    if (!ed || ed->selected < 0 || ed->selected >= ed->item_count) return -1;
    return ed->items[ed->selected].node;
}

/* ------------------------------------------------------------ the tools */

SDGE_API void sdge_editor_set_tool(sdge_editor *ed, int tool) {
    if (!ed) return;
    if (tool < SDGE_EDIT_SELECT || tool > SDGE_EDIT_SCALE) return;
    ed->tool = tool;
}

SDGE_API int sdge_editor_tool(const sdge_editor *ed) {
    return ed ? ed->tool : SDGE_EDIT_SELECT;
}

SDGE_API void sdge_editor_set_axis(sdge_editor *ed, int axis) {
    if (!ed) return;
    if (axis < SDGE_EDIT_AXIS_NONE || axis > SDGE_EDIT_AXIS_Z) return;
    ed->axis = axis;
}

SDGE_API int sdge_editor_axis(const sdge_editor *ed) {
    return ed ? ed->axis : SDGE_EDIT_AXIS_NONE;
}

/* ---------------------------------------------------------- the transform */

/* the local transform of an item (an object row or a glTF node row) */
static int ed_item_get_local(sdge_editor *ed, int item, sdg_vec3 t,
                             sdg_quat r, sdg_vec3 s) {
    if (!ed || item < 0 || item >= ed->item_count) return -1;
    sdge_edit_item *it = &ed->items[item];
    ed_object *o = ed_get_object(ed, it->object);
    if (!o) return -1;
    if (it->node < 0) {
        if (t) v3_copy(t, o->pos);
        if (r) q_copy(r, o->rot);
        if (s) v3_copy(s, o->scale);
        return 0;
    }
    if (!o->model) return -1;
    sdge_model_node_get_transform(o->model, it->node, t, r, s);
    return 0;
}

static int ed_item_set_local(sdge_editor *ed, int item, const sdg_vec3 t,
                             const sdg_quat r, const sdg_vec3 s) {
    if (!ed || item < 0 || item >= ed->item_count) return -1;
    sdge_edit_item *it = &ed->items[item];
    ed_object *o = ed_get_object(ed, it->object);
    if (!o) return -1;
    if (it->node < 0) {
        if (t) v3_copy(o->pos, t);
        if (r) q_copy(o->rot, r);
        if (s) v3_copy(o->scale, s);
    } else {
        if (!o->model) return -1;
        sdge_model_node_set_transform(o->model, it->node, t, r, s);
        sdge_model_refresh(o->model);
    }
    ed_dirty(ed);
    return 0;
}

/* the world matrix of an item (the object transform is the parent of every
 * glTF node) */
static int ed_item_world(sdge_editor *ed, int item, sdg_mat4 out) {
    if (!ed || item < 0 || item >= ed->item_count) return -1;
    sdge_edit_item *it = &ed->items[item];
    ed_object *o = ed_get_object(ed, it->object);
    if (!o) return -1;
    sdg_mat4 om;
    ed_object_matrix(o, om);
    if (it->node < 0) {
        m4_copy(out, om);
        return 0;
    }
    if (!o->model) return -1;
    sdg_mat4 nw;
    sdge_model_node_world(o->model, it->node, nw);
    m4_mul(out, om, nw);
    return 0;
}

/* the world matrix of the parent of an item */
static int ed_item_parent_world(sdge_editor *ed, int item, sdg_mat4 out) {
    if (!ed || item < 0 || item >= ed->item_count) return -1;
    sdge_edit_item *it = &ed->items[item];
    ed_object *o = ed_get_object(ed, it->object);
    if (!o) return -1;
    m4_identity(out);
    if (it->node < 0) return 0;
    if (!o->model) return -1;
    sdg_mat4 om;
    ed_object_matrix(o, om);
    int parent = sdge_model_node_parent(o->model, it->node);
    if (parent < 0) {
        m4_copy(out, om);
        return 0;
    }
    sdg_mat4 pw;
    sdge_model_node_world(o->model, parent, pw);
    m4_mul(out, om, pw);
    return 0;
}

/* a world direction in the space of the parent of an item */
static void ed_world_to_item_parent(sdge_editor *ed, int item,
                                    const float dir[3], float out[3]) {
    sdg_mat4 pw, inv;
    if (ed_item_parent_world(ed, item, pw) != 0) m4_identity(pw);
    m4_invert(inv, pw);
    sdg_vec4 v = { dir[0], dir[1], dir[2], 0.0f };
    sdg_vec4 r;
    m4_mul_vec4(r, inv, v);
    v3_set(out, r[0], r[1], r[2]);
    v3_norm(out, out);
}

SDGE_API int sdge_editor_get_transform(sdge_editor *ed, sdg_vec3 t,
                                       sdg_quat r, sdg_vec3 s) {
    if (!ed) return -1;
    return ed_item_get_local(ed, ed->selected, t, r, s);
}

SDGE_API int sdge_editor_set_transform(sdge_editor *ed, const sdg_vec3 t,
                                       const sdg_quat r, const sdg_vec3 s) {
    if (!ed) return -1;
    return ed_item_set_local(ed, ed->selected, t, r, s);
}

/* -------------------------------------------------------------- the camera */

SDGE_API sdge_camera3d *sdge_editor_camera(sdge_editor *ed) {
    return ed ? &ed->cam : NULL;
}

SDGE_API void sdge_editor_orbit(sdge_editor *ed, float yaw, float pitch,
                                float dist) {
    if (!ed) return;
    ed->cam.yaw = yaw;
    ed->cam.pitch = pitch;
    if (dist > 0.0f) ed->cam.dist = dist;
    if (ed->cam.pitch >  1.5f) ed->cam.pitch =  1.5f;
    if (ed->cam.pitch < -1.5f) ed->cam.pitch = -1.5f;
}

SDGE_API void sdge_editor_update_camera(sdge_editor *ed, int width, int height) {
    if (!ed) return;
    if (width > 0 && height > 0)
        sdge_camera3d_set_aspect(&ed->cam, (float)width / (float)height);
    sdge_camera3d_update(&ed->cam);
}

/* adds the world bounds of an object to a box */
static void ed_object_bounds(sdge_editor *ed, int object, float mn[3],
                             float mx[3], int *found) {
    ed_object *o = ed_get_object(ed, object);
    if (!o || !o->visible) return;
    sdg_mat4 om;
    ed_object_matrix(o, om);

    if (!o->model) {
        float c[3] = { o->pos[0], o->pos[1], o->pos[2] };
        for (int k = 0; k < 3; ++k) {
            if (c[k] < mn[k]) mn[k] = c[k];
            if (c[k] > mx[k]) mx[k] = c[k];
        }
        *found = 1;
        return;
    }

    int count = sdge_model_node_count(o->model);
    for (int n = 0; n < count; ++n) {
        const sdge_gltf_model *g = sdge_model_gltf(o->model);
        if (!g || n >= g->node_count || g->nodes[n].mesh < 0) continue;
        float lo[3], hi[3];
        sdge_model_node_bounds(o->model, n, lo, hi);
        for (int c = 0; c < 8; ++c) {
            float p[3] = {
                (c & 1) ? hi[0] : lo[0],
                (c & 2) ? hi[1] : lo[1],
                (c & 4) ? hi[2] : lo[2]
            };
            float w[3];
            m4_mul_point(w, om, p);
            for (int k = 0; k < 3; ++k) {
                if (w[k] < mn[k]) mn[k] = w[k];
                if (w[k] > mx[k]) mx[k] = w[k];
            }
        }
        *found = 1;
    }
}

SDGE_API void sdge_editor_focus(sdge_editor *ed) {
    if (!ed) return;
    float mn[3] = { 1e30f, 1e30f, 1e30f };
    float mx[3] = { -1e30f, -1e30f, -1e30f };
    int found = 0;
    for (int i = 0; i < ed->object_count; ++i)
        ed_object_bounds(ed, i, mn, mx, &found);
    if (!found) {
        v3_set(mn, -1.0f, -1.0f, -1.0f);
        v3_set(mx,  1.0f,  1.0f,  1.0f);
    }
    for (int k = 0; k < 3; ++k)
        ed->focus_center[k] = (mn[k] + mx[k]) * 0.5f;
    float r = 0.0f;
    for (int k = 0; k < 3; ++k) {
        float d = (mx[k] - mn[k]) * 0.5f;
        r += d * d;
    }
    ed->focus_radius = sqrtf(r);
    if (ed->focus_radius < 0.5f) ed->focus_radius = 0.5f;
    ed->cam.dist = ed->focus_radius / tanf(0.5f * 45.0f * SDG_PI / 180.0f) *
                   1.3f;
    ed->cam.center[0] = ed->focus_center[0];
    ed->cam.center[1] = ed->focus_center[1];
    ed->cam.center[2] = ed->focus_center[2];
}

SDGE_API void sdge_editor_set_viewport(sdge_editor *ed, int x, int y,
                                       int w, int h) {
    if (!ed) return;
    ed->vp_x = x;
    ed->vp_y = y;
    ed->vp_w = w > 0 ? w : 1;
    ed->vp_h = h > 0 ? h : 1;
}

/* --------------------------------------------------------------- picking */

SDGE_API int sdge_editor_pick(sdge_editor *ed, float x, float y) {
    if (!ed) return -1;
    sdge_camera3d_update(&ed->cam);

    float ro[3], rd[3];
    ed_ray(ed, x, y, ro, rd);

    float best_t = 1e30f;
    int best_item = -1;

    for (int item = 0; item < ed->item_count; ++item) {
        sdge_edit_item *it = &ed->items[item];
        if (it->node < 0) continue;
        ed_object *o = ed_get_object(ed, it->object);
        if (!o || !o->visible || !o->model) continue;
        const sdge_gltf_model *g = sdge_model_gltf(o->model);
        if (!g || it->node >= g->node_count) continue;
        int mesh_index = g->nodes[it->node].mesh;
        if (mesh_index < 0 || mesh_index >= g->mesh_count) continue;

        /* the ray in the space of the scene object */
        sdg_mat4 om, inv_obj;
        ed_object_matrix(o, om);
        m4_invert(inv_obj, om);
        sdg_vec4 ro4 = { ro[0], ro[1], ro[2], 1.0f };
        sdg_vec4 rd4 = { rd[0], rd[1], rd[2], 0.0f };
        sdg_vec4 oo4, od4;
        m4_mul_vec4(oo4, inv_obj, ro4);
        m4_mul_vec4(od4, inv_obj, rd4);
        float oo[3] = { oo4[0], oo4[1], oo4[2] };
        float od[3] = { od4[0], od4[1], od4[2] };

        /* the cheap test: the bounding box of the node */
        float lo[3], hi[3];
        sdge_model_node_bounds(o->model, it->node, lo, hi);
        float t0, t1;
        if (!ray_box(oo, od, lo, hi, &t0, &t1)) continue;
        if (t0 > best_t) continue;

        /* the ray in the local space of the node */
        sdg_mat4 world, inv;
        sdge_model_node_world(o->model, it->node, world);
        m4_invert(inv, world);
        sdg_vec4 lno, lnd;
        m4_mul_vec4(lno, inv, oo4);
        m4_mul_vec4(lnd, inv, od4);
        float lo_l[3] = { lno[0], lno[1], lno[2] };
        float ld_l[3] = { lnd[0], lnd[1], lnd[2] };

        const sdge_gltf_mesh *mesh = &g->meshes[mesh_index];
        for (int p = 0; p < mesh->prim_count; ++p) {
            const sdge_primitive *prim = &mesh->prims[p];
            const sdge_accessor *pa = &prim->positions;
            if (pa->count <= 0) continue;
            int count = prim->indices.count > 0 ? prim->indices.count
                                                : pa->count;
            for (int i = 0; i + 2 < count; i += 3) {
                int i0, i1, i2;
                if (prim->indices.count > 0) {
                    i0 = acc_index(&prim->indices, i);
                    i1 = acc_index(&prim->indices, i + 1);
                    i2 = acc_index(&prim->indices, i + 2);
                } else {
                    i0 = i; i1 = i + 1; i2 = i + 2;
                }
                if (i0 < 0 || i1 < 0 || i2 < 0) continue;
                if (i0 >= pa->count || i1 >= pa->count || i2 >= pa->count)
                    continue;
                float a[3], b[3], c[3];
                for (int k = 0; k < 3; ++k) {
                    a[k] = acc_get(pa, i0, k);
                    b[k] = acc_get(pa, i1, k);
                    c[k] = acc_get(pa, i2, k);
                }
                float t;
                if (!ray_triangle(lo_l, ld_l, a, b, c, &t)) continue;
                if (t < 0.0f) continue;
                if (t < best_t) {
                    best_t = t;
                    best_item = item;
                }
                break;      /* one hit per primitive is enough */
            }
        }
    }
    return best_item;
}

/* the ray against an axis aligned box (the slab method) */
static int ray_box(const float o[3], const float d[3], const float lo[3],
                   const float hi[3], float *t0_out, float *t1_out) {
    float t0 = -1e30f, t1 = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (fabsf(d[i]) < 1e-9f) {
            if (o[i] < lo[i] || o[i] > hi[i]) return 0;
            continue;
        }
        float inv = 1.0f / d[i];
        float ta = (lo[i] - o[i]) * inv;
        float tb = (hi[i] - o[i]) * inv;
        if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
        if (t0 > t1) return 0;
    }
    if (t0_out) *t0_out = t0;
    if (t1_out) *t1_out = t1;
    return 1;
}

/* ----------------------------------------------------------------- input */

SDGE_API void sdge_editor_mouse_move(sdge_editor *ed, float x, float y) {
    if (!ed) return;
    ed->last_mx = x;
    ed->last_my = y;
    if (!ed->dragging) return;
    if (ed->drag_item < 0 || ed->drag_item >= ed->item_count) {
        ed->dragging = 0;
        return;
    }

    float dx = x - ed->drag_mx;
    float dy = y - ed->drag_my;

    if (ed->drag_tool == SDGE_EDIT_MOVE) {
        /* the point of the mouse on the plane of the drag */
        float ro[3], rd[3];
        ed_ray(ed, x, y, ro, rd);
        float hit[3];
        if (!ray_plane(ro, rd, ed->drag_plane, ed->drag_plane[3], hit))
            return;
        float delta[3];
        v3_sub(delta, hit, ed->drag_origin);
        /* the axis constraint */
        if (ed->axis != SDGE_EDIT_AXIS_NONE) {
            int axis = ed->axis - 1;
            float d = delta[axis];
            delta[0] = delta[1] = delta[2] = 0.0f;
            delta[axis] = d;
        }
        /* the delta in the space of the parent */
        float local[3];
        ed_world_to_item_parent(ed, ed->drag_item, delta, local);
        sdg_vec3 t, s;
        sdg_quat r;
        ed_item_get_local(ed, ed->drag_item, t, r, s);
        t[0] = ed->drag_translation[0] + local[0];
        t[1] = ed->drag_translation[1] + local[1];
        t[2] = ed->drag_translation[2] + local[2];
        ed_item_set_local(ed, ed->drag_item, t, r, s);
    } else if (ed->drag_tool == SDGE_EDIT_ROTATE) {
        float angle;
        float axis_world[3];
        if (ed->axis != SDGE_EDIT_AXIS_NONE) {
            int a = ed->axis - 1;
            v3_set(axis_world, a == 0 ? 1.0f : 0.0f,
                             a == 1 ? 1.0f : 0.0f,
                             a == 2 ? 1.0f : 0.0f);
            angle = dx * 0.01f;
        } else {
            /* around the axis of the camera */
            float fwd[3], right[3], up[3] = { 0.0f, 1.0f, 0.0f };
            sdge_camera3d_forward(&ed->cam, fwd);
            v3_cross(right, fwd, up);
            v3_norm(right, right);
            if (fabsf(dy) > fabsf(dx)) {
                v3_copy(axis_world, right);
                angle = dy * 0.01f;
            } else {
                v3_copy(axis_world, fwd);
                angle = dx * 0.01f;
            }
        }
        float axis_local[3];
        ed_world_to_item_parent(ed, ed->drag_item, axis_world, axis_local);
        sdg_quat dq;
        q_from_axis_angle(dq, axis_local, angle);
        sdg_quat nr;
        q_mul(nr, dq, ed->drag_rot);
        q_norm(nr, nr);
        sdg_vec3 t, s;
        sdg_quat r;
        ed_item_get_local(ed, ed->drag_item, t, r, s);
        ed_item_set_local(ed, ed->drag_item, t, nr, s);
    } else if (ed->drag_tool == SDGE_EDIT_SCALE) {
        float factor = 1.0f + dx * 0.01f;
        if (factor < 0.05f) factor = 0.05f;
        if (factor > 20.0f) factor = 20.0f;
        sdg_vec3 t, s;
        sdg_quat r;
        ed_item_get_local(ed, ed->drag_item, t, r, s);
        sdg_vec3 ns;
        v3_set(ns, ed->drag_scale[0] * factor,
                   ed->drag_scale[1] * factor,
                   ed->drag_scale[2] * factor);
        ed_item_set_local(ed, ed->drag_item, t, r, ns);
    }
}

SDGE_API void sdge_editor_mouse_button(sdge_editor *ed, int button, int action) {
    if (!ed) return;
    if (button != 0) return;

    if (action == 1) {                    /* press */
        if (ed->tool == SDGE_EDIT_SELECT) {
            int item = sdge_editor_pick(ed, ed->last_mx, ed->last_my);
            sdge_editor_select(ed, item);
            return;
        }
        if (ed->selected < 0 || ed->selected >= ed->item_count) return;
        sdg_mat4 world;
        if (ed_item_world(ed, ed->selected, world) != 0) return;
        ed->dragging = 1;
        ed->drag_item = ed->selected;
        ed->drag_tool = ed->tool;
        ed->drag_mx = ed->last_mx;
        ed->drag_my = ed->last_my;
        v3_set(ed->drag_origin, world[12], world[13], world[14]);
        ed_item_get_local(ed, ed->selected, ed->drag_translation,
                          &ed->drag_rot[0], ed->drag_scale);
        /* the plane of the move: the normal is the view direction */
        float fwd[3];
        sdge_camera3d_forward(&ed->cam, fwd);
        v3_copy(ed->drag_plane, fwd);
        ed->drag_plane[3] = -v3_dot(ed->drag_origin, fwd);
    } else {                              /* release */
        ed->dragging = 0;
    }
}

SDGE_API void sdge_editor_scroll(sdge_editor *ed, float dy) {
    if (!ed) return;
    ed->cam.dist -= dy * 0.02f;
    if (ed->cam.dist < 0.05f) ed->cam.dist = 0.05f;
    if (ed->cam.dist > 5000.0f) ed->cam.dist = 5000.0f;
}

SDGE_API void sdge_editor_key(sdge_editor *ed, int key) {
    if (!ed) return;
    switch (key) {
    case '1': case 'q': case 'Q': ed->tool = SDGE_EDIT_SELECT; break;
    case '2': case 'w': case 'W': ed->tool = SDGE_EDIT_MOVE;   break;
    case '3': case 'e': case 'E': ed->tool = SDGE_EDIT_ROTATE; break;
    case '4': case 'r': case 'R': ed->tool = SDGE_EDIT_SCALE;  break;
    case 'x': case 'X':
        ed->axis = (ed->axis == SDGE_EDIT_AXIS_X) ? SDGE_EDIT_AXIS_NONE
                                                   : SDGE_EDIT_AXIS_X;
        break;
    case 'y': case 'Y':
        ed->axis = (ed->axis == SDGE_EDIT_AXIS_Y) ? SDGE_EDIT_AXIS_NONE
                                                   : SDGE_EDIT_AXIS_Y;
        break;
    case 'z': case 'Z':
        ed->axis = (ed->axis == SDGE_EDIT_AXIS_Z) ? SDGE_EDIT_AXIS_NONE
                                                   : SDGE_EDIT_AXIS_Z;
        break;
    case 27: ed->axis = SDGE_EDIT_AXIS_NONE; break;
    default: break;
    }
}

/* --------------------------------------------------------------- drawing */

static void ed_draw_grid(float extent, int divisions) {
    if (divisions < 1) divisions = 1;
    int lines = (divisions + 1) * 2;
    float *verts = (float *)calloc((size_t)lines * 6, sizeof(float));
    if (!verts) return;
    float step = (extent * 2.0f) / (float)divisions;
    int n = 0;
    for (int i = 0; i <= divisions; ++i) {
        float p = -extent + step * (float)i;
        /* along X */
        verts[n++] = -extent; verts[n++] = 0.0f; verts[n++] = p;
        verts[n++] =  extent; verts[n++] = 0.0f; verts[n++] = p;
        /* along Z */
        verts[n++] = p; verts[n++] = 0.0f; verts[n++] = -extent;
        verts[n++] = p; verts[n++] = 0.0f; verts[n++] =  extent;
    }
    const float grid_color[4] = { 0.30f, 0.31f, 0.33f, 1.0f };
    sdgr_draw_lines(verts, n / 3, grid_color);
    free(verts);

    float axis_x[6] = { -extent, 0.0f, 0.0f, extent, 0.0f, 0.0f };
    float axis_z[6] = { 0.0f, 0.0f, -extent, 0.0f, 0.0f, extent };
    sdgr_draw_lines(axis_x, 2, COL_AXIS_X);
    sdgr_draw_lines(axis_z, 2, COL_AXIS_Z);
}

static void ed_draw_item_box(sdge_editor *ed, int item) {
    if (!ed || item < 0 || item >= ed->item_count) return;
    sdge_edit_item *it = &ed->items[item];
    ed_object *o = ed_get_object(ed, it->object);
    if (!o) return;

    float mn[3] = { 1e30f, 1e30f, 1e30f };
    float mx[3] = { -1e30f, -1e30f, -1e30f };

    if (it->node >= 0 && o->model) {
        float lo[3], hi[3];
        sdge_model_node_bounds(o->model, it->node, lo, hi);
        sdg_mat4 om;
        ed_object_matrix(o, om);
        for (int c = 0; c < 8; ++c) {
            float p[3] = {
                (c & 1) ? hi[0] : lo[0],
                (c & 2) ? hi[1] : lo[1],
                (c & 4) ? hi[2] : lo[2]
            };
            float w[3];
            m4_mul_point(w, om, p);
            for (int k = 0; k < 3; ++k) {
                if (w[k] < mn[k]) mn[k] = w[k];
                if (w[k] > mx[k]) mx[k] = w[k];
            }
        }
    } else {
        int found = 0;
        ed_object_bounds(ed, it->object, mn, mx, &found);
        if (!found) {
            v3_set(mn, o->pos[0] - 0.5f, o->pos[1] - 0.5f, o->pos[2] - 0.5f);
            v3_set(mx, o->pos[0] + 0.5f, o->pos[1] + 0.5f, o->pos[2] + 0.5f);
        }
    }
    float center[3], half[3];
    for (int k = 0; k < 3; ++k) {
        center[k] = (mn[k] + mx[k]) * 0.5f;
        half[k] = (mx[k] - mn[k]) * 0.5f;
        if (half[k] < 0.02f) half[k] = 0.02f;
    }
    sdgr_draw_box_wire(center, half, NULL, COL_SELECT);
}

SDGE_API void sdge_editor_render(sdge_editor *ed) {
    if (!ed) return;
    sdge_camera3d_update(&ed->cam);

    /* the Unity-like ground grid of the scene view */
    if (ed->grid_visible) ed_draw_grid(10.0f, 20);

    for (int i = 0; i < ed->object_count; ++i) {
        ed_object *o = &ed->objects[i];
        if (!o->visible || !o->model) continue;
        sdg_mat4 om;
        ed_object_matrix(o, om);
        sdge_model_draw(o->model, om);
    }

    int item = ed->selected;
    if (item < 0 || item >= ed->item_count) return;

    ed_draw_item_box(ed, item);

    sdg_mat4 world;
    if (ed_item_world(ed, item, world) != 0) return;
    float origin[3] = { world[12], world[13], world[14] };

    /* the axes of the gizmo */
    float len = ed->cam.dist * 0.2f;
    if (len < 0.05f) len = 0.05f;
    float lines[18];
    for (int i = 0; i < 3; ++i) {
        float tip[3];
        v3_copy(tip, origin);
        tip[i] += len;
        lines[i * 6 + 0] = origin[0];
        lines[i * 6 + 1] = origin[1];
        lines[i * 6 + 2] = origin[2];
        lines[i * 6 + 3] = tip[0];
        lines[i * 6 + 4] = tip[1];
        lines[i * 6 + 5] = tip[2];
    }
    sdgr_draw_lines_always(lines, 6, COL_AXIS_X);
    sdgr_draw_lines_always(lines + 6, 6, COL_AXIS_Y);
    sdgr_draw_lines_always(lines + 12, 6, COL_AXIS_Z);
}

/* -------------------------------------------------------------- play mode */

SDGE_API void sdge_editor_set_play_state(sdge_editor *ed, int state) {
    if (!ed) return;
    if (state < SDGE_EDIT_PLAY_STOPPED || state > SDGE_EDIT_PLAY_PAUSED)
        return;
    if (state == ed->play_state) return;

    if (state == SDGE_EDIT_PLAY_PLAYING && ed->play_state == SDGE_EDIT_PLAY_STOPPED) {
        for (int i = 0; i < ed->object_count; ++i) {
            sdge_model *m = ed->objects[i].model;
            if (m && sdge_model_anim_count(m) > 0) sdge_model_play(m, 0, 1);
        }
    } else if (state == SDGE_EDIT_PLAY_STOPPED) {
        for (int i = 0; i < ed->object_count; ++i) {
            sdge_model *m = ed->objects[i].model;
            if (m) sdge_model_stop(m);
        }
    }
    ed->play_state = state;
}

SDGE_API int sdge_editor_play_state(const sdge_editor *ed) {
    return ed ? ed->play_state : SDGE_EDIT_PLAY_STOPPED;
}

SDGE_API void sdge_editor_advance(sdge_editor *ed, float dt) {
    if (!ed || ed->play_state != SDGE_EDIT_PLAY_PLAYING) return;
    if (dt <= 0.0f) return;
    for (int i = 0; i < ed->object_count; ++i) {
        sdge_model *m = ed->objects[i].model;
        if (m) sdge_model_advance(m, dt);
    }
}

SDGE_API void sdge_editor_set_grid_visible(sdge_editor *ed, int visible) {
    if (ed) ed->grid_visible = visible ? 1 : 0;
}

SDGE_API int sdge_editor_grid_visible(const sdge_editor *ed) {
    return ed ? ed->grid_visible : 0;
}

/* -------------------------------------------------------------- textures */

SDGE_API const char *sdge_editor_texture_path(sdge_editor *ed, int node,
                                               int slot) {
    return sdge_editor_texture_path_for(ed, ed ? ed->active : -1, node, slot);
}

SDGE_API const char *sdge_editor_texture_path_for(sdge_editor *ed, int object,
                                                   int node, int slot) {
    return ed_tex_find(ed, object, node, slot);
}

SDGE_API int sdge_editor_apply_texture(sdge_editor *ed, int item,
                                        const char *path, int slot) {
    if (!ed) return -1;
    ed_clear_error(ed);
    if (item < 0 || item >= ed->item_count) {
        ed_error(ed, "nothing is selected");
        return -1;
    }
    sdge_edit_item *it = &ed->items[item];
    if (it->node < 0) {
        ed_error(ed, "select a mesh node, not the object");
        return -1;
    }
    if (!path || !path[0]) {
        ed_error(ed, "no texture file");
        return -1;
    }
    if (slot < 0 || slot >= SDGR_TEX_SLOTS) {
        ed_error(ed, "unknown texture slot");
        return -1;
    }
    if (ed_apply_texture_node(ed, it->object, it->node, path, slot) != 0) {
        ed_error(ed, "cannot decode or upload the image");
        return -1;
    }
    ed_tex_set(ed, it->object, it->node, slot, path);
    ed_dirty(ed);
    return 0;
}

SDGE_API int sdge_editor_clear_texture(sdge_editor *ed, int item, int slot) {
    if (!ed || item < 0 || item >= ed->item_count) return -1;
    sdge_edit_item *it = &ed->items[item];
    ed_object *o = ed_get_object(ed, it->object);
    if (!o || !o->model || it->node < 0) return -1;
    ed_tex_set(ed, it->object, it->node, slot, NULL);
    ed_dirty(ed);
    return sdge_model_set_node_texture(o->model, it->node, slot, 0);
}

/* ---------------------------------------------------------- scene file IO */

static void ed_json_string(FILE *f, const char *text) {
    fputc('"', f);
    for (const char *p = text ? text : ""; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            fputc('\\', f);
            fputc(c, f);
        } else if (c == '\n') {
            fputs("\\n", f);
        } else if (c == '\r') {
            fputs("\\r", f);
        } else if (c == '\t') {
            fputs("\\t", f);
        } else if (c < 0x20) {
            fprintf(f, "\\u%04x", (unsigned)c);
        } else {
            fputc(c, f);
        }
    }
    fputc('"', f);
}

static void ed_json_vec(FILE *f, const char *key, const float *v, int n) {
    fprintf(f, "      \"%s\": [", key);
    for (int i = 0; i < n; ++i)
        fprintf(f, "%s%.9g", i ? ", " : "", v[i]);
    fputs("]", f);
}

SDGE_API int sdge_editor_save_scene(sdge_editor *ed, const char *path) {
    if (!ed || !path || !path[0]) return -1;
    ed_clear_error(ed);
    ed_make_dirs(path);
    FILE *f = fopen(path, "wb");
    if (!f) {
        ed_error(ed, "cannot write the scene file");
        return -1;
    }

    fputs("{\n", f);
    fputs("  \"format\": \"sdge-scene\",\n", f);
    fputs("  \"version\": 1,\n", f);
    fputs("  \"name\": ", f);
    ed_json_string(f, ed->scene_name);
    fputs(",\n", f);
    fprintf(f, "  \"camera\": {\"center\": [%.9g, %.9g, %.9g], "
               "\"dist\": %.9g, \"yaw\": %.9g, \"pitch\": %.9g},\n",
            ed->cam.center[0], ed->cam.center[1], ed->cam.center[2],
            ed->cam.dist, ed->cam.yaw, ed->cam.pitch);
    fputs("  \"objects\": [\n", f);

    for (int i = 0; i < ed->object_count; ++i) {
        ed_object *o = &ed->objects[i];
        fputs("    {\n      \"name\": ", f);
        ed_json_string(f, o->name);
        fputs(",\n      \"model\": ", f);
        ed_json_string(f, o->path);
        fprintf(f, ",\n      \"visible\": %s,\n", o->visible ? "true" : "false");
        fprintf(f, "      \"expanded\": %s,\n", o->expanded ? "true" : "false");
        ed_json_vec(f, "position", o->pos, 3);
        fputs(",\n", f);
        ed_json_vec(f, "rotation", o->rot, 4);
        fputs(",\n", f);
        ed_json_vec(f, "scale", o->scale, 3);
        fputs(",\n", f);

        fputs("      \"nodes\": [", f);
        int node_count = o->model ? sdge_model_node_count(o->model) : 0;
        for (int n = 0; n < node_count; ++n) {
            sdg_vec3 t, s;
            sdg_quat r;
            sdge_model_node_get_transform(o->model, n, t, r, s);
            fprintf(f, "%s\n        {\"node\": %d, \"position\": "
                       "[%.9g, %.9g, %.9g], \"rotation\": "
                       "[%.9g, %.9g, %.9g, %.9g], \"scale\": "
                       "[%.9g, %.9g, %.9g]}",
                    n ? "," : "", n, t[0], t[1], t[2], r[0], r[1], r[2], r[3],
                    s[0], s[1], s[2]);
        }
        fputs(node_count ? "\n      ],\n" : "],\n", f);

        fputs("      \"textures\": [", f);
        int tex_written = 0;
        for (int k = 0; k < ed->tex_ref_count; ++k) {
            ed_tex_ref *tr = &ed->tex_refs[k];
            if (tr->object != i) continue;
            fprintf(f, "%s\n        {\"node\": %d, \"slot\": %d, \"file\": ",
                    tex_written ? "," : "", tr->node, tr->slot);
            ed_json_string(f, tr->path);
            fputs("}", f);
            tex_written = 1;
        }
        fputs(tex_written ? "\n      ]\n" : "]\n", f);
        fprintf(f, "    }%s\n", (i + 1 < ed->object_count) ? "," : "");
    }
    fputs("  ]\n}\n", f);
    int bad = ferror(f);
    fclose(f);
    if (bad) {
        ed_error(ed, "cannot finish the scene file");
        return -1;
    }

    snprintf(ed->scene_path, sizeof(ed->scene_path), "%s", path);
    ed->scene_dirty = 0;
    return 0;
}

static int ed_json_vec_read(const sdj_doc *d, int tok, float *out, int n) {
    if (tok < 0 || d->tok[tok].type != SDJ_ARRAY) return -1;
    for (int i = 0; i < n; ++i) {
        int e = sdj_arr_at(d, tok, i);
        if (e < 0 || !sdj_is_num(d, e)) return -1;
        out[i] = (float)sdj_num(d, e);
    }
    return 0;
}

SDGE_API int sdge_editor_load_scene(sdge_editor *ed, const char *path) {
    if (!ed || !path || !path[0]) return -1;
    ed_clear_error(ed);

    unsigned char *data = NULL;
    size_t size = 0;
    if (sdga_read_file(path, &data, &size) != 0 || !data) {
        ed_error(ed, "cannot read the scene file");
        return -1;
    }
    int tokens = sdj_count_tokens((const char *)data) + 16;
    sdj_token *tok = (sdj_token *)malloc(sizeof(sdj_token) * (size_t)tokens);
    if (!tok) {
        free(data);
        ed_error(ed, "no memory for the scene");
        return -1;
    }
    sdj_doc d;
    sdj_init_ex(&d, (const char *)data, tok, tokens);
    if (d.count <= 0) {
        free(tok);
        free(data);
        ed_error(ed, "the scene file is not valid JSON");
        return -1;
    }
    int root = 0;
    if (d.tok[root].type != SDJ_OBJECT) {
        free(tok);
        free(data);
        ed_error(ed, "the scene root is not an object");
        return -1;
    }

    char dir[EDIT_PATH_MAX];
    ed_dir_of(path, dir, sizeof(dir));

    sdge_editor_new_scene(ed, "Scene");
    int name_tok = sdj_obj_get(&d, root, "name");
    if (name_tok >= 0) {
        char name[96];
        if (sdj_str(&d, name_tok, name, sizeof(name)) >= 0)
            snprintf(ed->scene_name, sizeof(ed->scene_name), "%s", name);
    }

    int objects = sdj_obj_get(&d, root, "objects");
    if (objects >= 0 && d.tok[objects].type == SDJ_ARRAY) {
        int count = sdj_len(&d, objects);
        for (int i = 0; i < count; ++i) {
            int e = sdj_arr_at(&d, objects, i);
            if (e < 0 || d.tok[e].type != SDJ_OBJECT) continue;

            char name[96] = "Object";
            char model[EDIT_PATH_MAX] = "";
            int name_t = sdj_obj_get(&d, e, "name");
            int model_t = sdj_obj_get(&d, e, "model");
            if (name_t >= 0) sdj_str(&d, name_t, name, sizeof(name));
            if (model_t >= 0) sdj_str(&d, model_t, model, sizeof(model));

            sdge_model *m = NULL;
            if (model[0]) {
                m = ed_load_model(model, dir);
                if (!m) {
                    /* keep the scene usable even when an asset is missing */
                    snprintf(ed->error, sizeof(ed->error),
                             "the model '%s' is missing", model);
                }
            }
            int obj = ed_add_object_with_model(ed, model, name, m);
            if (obj < 0) {
                if (m) sdge_model_destroy(m);
                continue;
            }
            ed_object *o = &ed->objects[obj];

            int t = sdj_obj_get(&d, e, "position");
            if (t >= 0) ed_json_vec_read(&d, t, o->pos, 3);
            t = sdj_obj_get(&d, e, "rotation");
            if (t >= 0) ed_json_vec_read(&d, t, o->rot, 4);
            t = sdj_obj_get(&d, e, "scale");
            if (t >= 0) ed_json_vec_read(&d, t, o->scale, 3);
            int vis = sdj_obj_get(&d, e, "visible");
            if (vis >= 0 && d.tok[vis].type == SDJ_PRIMITIVE)
                o->visible = (sdj_num(&d, vis) != 0.0);
            int exp = sdj_obj_get(&d, e, "expanded");
            if (exp >= 0 && d.tok[exp].type == SDJ_PRIMITIVE)
                o->expanded = (sdj_num(&d, exp) != 0.0);

            if (o->model) {
                int nodes = sdj_obj_get(&d, e, "nodes");
                if (nodes >= 0 && d.tok[nodes].type == SDJ_ARRAY) {
                    int nc = sdj_len(&d, nodes);
                    for (int k = 0; k < nc; ++k) {
                        int nt = sdj_arr_at(&d, nodes, k);
                        if (nt < 0 || d.tok[nt].type != SDJ_OBJECT) continue;
                        int ni = (int)sdj_num(&d, sdj_obj_get(&d, nt, "node"));
                        if (ni < 0 || ni >= sdge_model_node_count(o->model))
                            continue;
                        sdg_vec3 p, s;
                        sdg_quat q;
                        sdge_model_node_get_transform(o->model, ni, p, q, s);
                        int v = sdj_obj_get(&d, nt, "position");
                        if (v >= 0) ed_json_vec_read(&d, v, p, 3);
                        v = sdj_obj_get(&d, nt, "rotation");
                        if (v >= 0) ed_json_vec_read(&d, v, q, 4);
                        v = sdj_obj_get(&d, nt, "scale");
                        if (v >= 0) ed_json_vec_read(&d, v, s, 3);
                        sdge_model_node_set_transform(o->model, ni, p, q, s);
                    }
                    sdge_model_refresh(o->model);
                }

                int texs = sdj_obj_get(&d, e, "textures");
                if (texs >= 0 && d.tok[texs].type == SDJ_ARRAY) {
                    int tc = sdj_len(&d, texs);
                    for (int k = 0; k < tc; ++k) {
                        int tt = sdj_arr_at(&d, texs, k);
                        if (tt < 0 || d.tok[tt].type != SDJ_OBJECT) continue;
                        int ni = (int)sdj_num(&d, sdj_obj_get(&d, tt, "node"));
                        int slot = (int)sdj_num(&d, sdj_obj_get(&d, tt, "slot"));
                        char file[EDIT_PATH_MAX] = "";
                        int ft = sdj_obj_get(&d, tt, "file");
                        if (ft >= 0) sdj_str(&d, ft, file, sizeof(file));
                        if (!file[0]) continue;
                        if (ed_apply_texture_node(ed, obj, ni, file, slot) == 0)
                            ed_tex_set(ed, obj, ni, slot, file);
                    }
                }
            }
        }
    }

    int has_camera = 0;
    int cam = sdj_obj_get(&d, root, "camera");
    if (cam >= 0 && d.tok[cam].type == SDJ_OBJECT) {
        int c = sdj_obj_get(&d, cam, "center");
        int dist = sdj_obj_get(&d, cam, "dist");
        int yaw = sdj_obj_get(&d, cam, "yaw");
        int pitch = sdj_obj_get(&d, cam, "pitch");
        if (c >= 0) has_camera |= ed_json_vec_read(&d, c, ed->cam.center, 3) == 0;
        if (dist >= 0) { ed->cam.dist = (float)sdj_num(&d, dist); has_camera = 1; }
        if (yaw >= 0) { ed->cam.yaw = (float)sdj_num(&d, yaw); has_camera = 1; }
        if (pitch >= 0) { ed->cam.pitch = (float)sdj_num(&d, pitch); has_camera = 1; }
    }

    /* free the JSON before the next scene reload */
    free(tok);
    free(data);

    ed_build_items(ed);
    ed->active = ed->object_count > 0 ? 0 : -1;
    ed->selected = -1;
    if (ed->active >= 0) sdge_editor_select_object(ed, ed->active);

    snprintf(ed->scene_path, sizeof(ed->scene_path), "%s", path);
    ed->scene_dirty = 0;
    if (!has_camera) sdge_editor_focus(ed);
    return 0;
}

SDGE_API const char *sdge_editor_scene_name(const sdge_editor *ed) {
    return ed ? ed->scene_name : "";
}

SDGE_API void sdge_editor_set_scene_name(sdge_editor *ed, const char *name) {
    if (!ed || !name) return;
    snprintf(ed->scene_name, sizeof(ed->scene_name), "%s", name);
    ed_dirty(ed);
}

SDGE_API const char *sdge_editor_scene_path(const sdge_editor *ed) {
    return ed ? ed->scene_path : "";
}

SDGE_API int sdge_editor_scene_dirty(const sdge_editor *ed) {
    return ed ? ed->scene_dirty : 0;
}

SDGE_API void sdge_editor_set_scene_dirty(sdge_editor *ed, int dirty) {
    if (ed) ed->scene_dirty = dirty ? 1 : 0;
}
