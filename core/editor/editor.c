/* editor.c - Silver Diamond Game Engine :: scene editor implementation
 *
 * The editor works on the glTF data of a model: every node of the file is
 * one row of the hierarchy and can be moved, turned, scaled and textured on
 * its own.  The changes are written into the model, so the export can save
 * them as a new glTF file.
 */
#include "editor.h"

#include "../assets/texture.h"
#include "../graphics/renderer.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define EDIT_MAX_ITEMS 8192
#define EDIT_PATH_MAX  512

/* the colours of the gizmo */
static const float COL_AXIS_X[4] = { 0.90f, 0.25f, 0.28f, 1.0f };
static const float COL_AXIS_Y[4] = { 0.35f, 0.85f, 0.35f, 1.0f };
static const float COL_AXIS_Z[4] = { 0.30f, 0.50f, 0.95f, 1.0f };
static const float COL_SELECT[4] = { 1.00f, 0.85f, 0.25f, 1.0f };

/* the file of every texture that was put on a node (the export needs it to
 * embed the image into the new glTF file) */
typedef struct {
    int  node;
    int  slot;
    char path[EDIT_PATH_MAX];
} ed_tex_ref;

struct sdge_editor {
    sdge_model *model;
    char path[EDIT_PATH_MAX];
    char error[256];

    sdge_edit_item items[EDIT_MAX_ITEMS];
    int item_count;
    int selected;

    int tool;
    int axis;

    sdge_camera3d cam;
    float focus_center[3];
    float focus_radius;

    int vp_x, vp_y, vp_w, vp_h;

    /* the last mouse position inside the viewport */
    float last_mx, last_my;

    /* the drag */
    int    dragging;
    int    drag_tool;
    float  drag_mx, drag_my;
    float  drag_origin[3];      /* the origin of the node at the start */
    sdg_quat drag_rot;
    sdg_vec3 drag_scale;
    sdg_vec3 drag_translation;
    float  drag_distance;       /* the distance to the camera */
    float  drag_plane[4];       /* the plane of the move (n, d) */

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

/* the world matrix of the parent of a node (identity without a parent) */
static void ed_parent_world(sdge_editor *ed, int node, sdg_mat4 out) {
    m4_identity(out);
    if (!ed->model) return;
    int parent = sdge_model_node_parent(ed->model, node);
    if (parent >= 0) sdge_model_node_world(ed->model, parent, out);
}

/* a world direction in the space of the parent of the node */
static void ed_world_to_parent(sdge_editor *ed, int node, const float dir[3],
                               float out[3]) {
    sdg_mat4 pw, inv;
    ed_parent_world(ed, node, pw);
    m4_invert(inv, pw);
    float v[4] = { dir[0], dir[1], dir[2], 0.0f };
    float r[4];
    m4_mul_vec4(r, inv, v);
    v3_set(out, r[0], r[1], r[2]);
    v3_norm(out, out);
}

/* --------------------------------------------------------- the life cycle */

SDGE_API sdge_editor *sdge_editor_create(void) {
    sdge_editor *ed = (sdge_editor *)calloc(1, sizeof(*ed));
    if (!ed) return NULL;
    ed->selected = -1;
    ed->tool = SDGE_EDIT_SELECT;
    ed->axis = SDGE_EDIT_AXIS_NONE;
    ed->vp_w = 1;
    ed->vp_h = 1;
    sdge_camera3d_init(&ed->cam, 45.0f, 1.0f, 0.05f, 1000.0f);
    ed->cam.dist = 6.0f;
    ed->cam.yaw = 0.7f;
    ed->cam.pitch = 0.25f;
    return ed;
}

SDGE_API void sdge_editor_destroy(sdge_editor *ed) {
    if (!ed) return;
    sdge_editor_close(ed);
    free(ed->tex_refs);
    free(ed);
}

SDGE_API void sdge_editor_close(sdge_editor *ed) {
    if (!ed) return;
    if (ed->model) {
        sdge_model_destroy(ed->model);
        ed->model = NULL;
    }
    ed->item_count = 0;
    ed->selected = -1;
    ed->tex_ref_count = 0;
    ed->path[0] = '\0';
}

/* flattens the node tree into the list of the rows */
static void ed_build_items(sdge_editor *ed) {
    ed->item_count = 0;
    if (!ed->model) return;
    const sdge_gltf_model *g = sdge_model_gltf(ed->model);
    if (!g) return;

    /* the roots first, then the children (a simple recursion) */
    int stack[64];
    int depths[64];
    int top = 0;
    for (int i = 0; i < g->root_count && top < 64; ++i) {
        stack[top] = g->roots[i];
        depths[top] = 0;
        ++top;
    }
    while (top > 0) {
        --top;
        int node = stack[top];
        int depth = depths[top];
        if (node < 0 || node >= g->node_count) continue;
        if (ed->item_count >= EDIT_MAX_ITEMS) break;
        sdge_edit_item *it = &ed->items[ed->item_count];
        it->node = node;
        it->depth = depth;
        it->has_mesh = (g->nodes[node].mesh >= 0);
        const char *name = g->nodes[node].name;
        if (!name || !name[0]) {
            if (it->has_mesh)
                snprintf(it->name, sizeof(it->name), "mesh %d",
                         g->nodes[node].mesh);
            else
                snprintf(it->name, sizeof(it->name), "node %d", node);
        } else {
            snprintf(it->name, sizeof(it->name), "%s", name);
        }
        ++ed->item_count;

        /* the children are pushed in the reverse order, so they come out
         * in the order of the file */
        int kids[64];
        int count = sdge_model_node_children(ed->model, node, kids, 64);
        for (int i = count - 1; i >= 0; --i) {
            if (top >= 64) break;
            stack[top] = kids[i];
            depths[top] = depth + 1;
            ++top;
        }
    }
}

SDGE_API int sdge_editor_open(sdge_editor *ed, const char *path) {
    if (!ed) return -1;
    ed_clear_error(ed);
    if (!path || !path[0]) {
        ed_error(ed, "no file name");
        return -1;
    }
    sdge_editor_close(ed);
    sdge_model *model = sdge_model_load(path);
    if (!model) {
        ed_error(ed, "cannot load the model");
        return -1;
    }
    ed->model = model;
    snprintf(ed->path, sizeof(ed->path), "%s", path);
    ed_build_items(ed);
    ed->selected = -1;
    sdge_editor_focus(ed);
    return 0;
}

SDGE_API const char *sdge_editor_path(const sdge_editor *ed) {
    return ed ? ed->path : "";
}

SDGE_API sdge_model *sdge_editor_model(const sdge_editor *ed) {
    return ed ? ed->model : NULL;
}

SDGE_API const char *sdge_editor_error(const sdge_editor *ed) {
    return ed ? ed->error : "";
}

/* --------------------------------------------------------- the hierarchy */

SDGE_API int sdge_editor_item_count(const sdge_editor *ed) {
    return ed ? ed->item_count : 0;
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

SDGE_API void sdge_editor_select(sdge_editor *ed, int item) {
    if (!ed) return;
    if (item < 0 || item >= ed->item_count) ed->selected = -1;
    else ed->selected = item;
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

SDGE_API int sdge_editor_get_transform(sdge_editor *ed, sdg_vec3 t, sdg_quat r,
                              sdg_vec3 s) {
    if (!ed || !ed->model) return -1;
    int node = sdge_editor_selected_node(ed);
    if (node < 0) return -1;
    sdge_model_node_get_transform(ed->model, node, t, r, s);
    return 0;
}

SDGE_API int sdge_editor_set_transform(sdge_editor *ed, const sdg_vec3 t,
                              const sdg_quat r, const sdg_vec3 s) {
    if (!ed || !ed->model) return -1;
    int node = sdge_editor_selected_node(ed);
    if (node < 0) return -1;
    sdge_model_node_set_transform(ed->model, node, t, r, s);
    sdge_model_refresh(ed->model);
    return 0;
}

/* -------------------------------------------------------------- the camera */

SDGE_API sdge_camera3d *sdge_editor_camera(sdge_editor *ed) {
    return ed ? &ed->cam : NULL;
}

SDGE_API void sdge_editor_orbit(sdge_editor *ed, float yaw, float pitch, float dist) {
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

SDGE_API void sdge_editor_focus(sdge_editor *ed) {
    if (!ed || !ed->model) return;
    const sdge_gltf_model *g = sdge_model_gltf(ed->model);
    if (!g) return;

    float mn[3] = { 1e30f, 1e30f, 1e30f };
    float mx[3] = { -1e30f, -1e30f, -1e30f };
    int found = 0;
    for (int i = 0; i < g->node_count; ++i) {
        if (g->nodes[i].mesh < 0) continue;
        float lo[3], hi[3];
        sdge_model_node_bounds(ed->model, i, lo, hi);
        for (int k = 0; k < 3; ++k) {
            if (lo[k] < mn[k]) mn[k] = lo[k];
            if (hi[k] > mx[k]) mx[k] = hi[k];
        }
        found = 1;
    }
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

SDGE_API void sdge_editor_set_viewport(sdge_editor *ed, int x, int y, int w, int h) {
    if (!ed) return;
    ed->vp_x = x;
    ed->vp_y = y;
    ed->vp_w = w > 0 ? w : 1;
    ed->vp_h = h > 0 ? h : 1;
}

/* --------------------------------------------------------------- picking */

SDGE_API int sdge_editor_pick(sdge_editor *ed, float x, float y) {
    if (!ed || !ed->model) return -1;
    sdge_camera3d_update(&ed->cam);

    float ro[3], rd[3];
    ed_ray(ed, x, y, ro, rd);

    const sdge_gltf_model *g = sdge_model_gltf(ed->model);
    if (!g) return -1;

    float best_t = 1e30f;
    int best_item = -1;

    for (int item = 0; item < ed->item_count; ++item) {
        int node = ed->items[item].node;
        int mesh_index = g->nodes[node].mesh;
        if (mesh_index < 0 || mesh_index >= g->mesh_count) continue;

        /* the cheap test: the bounding box of the node */
        float lo[3], hi[3];
        sdge_model_node_bounds(ed->model, node, lo, hi);
        float t0, t1;
        if (!ray_box(ro, rd, lo, hi, &t0, &t1)) continue;
        if (t0 > best_t) continue;

        /* the ray in the local space of the node */
        sdg_mat4 world, inv;
        sdge_model_node_world(ed->model, node, world);
        m4_invert(inv, world);
        float lo4[4] = { ro[0], ro[1], ro[2], 1.0f };
        float ld4[4] = { rd[0], rd[1], rd[2], 0.0f };
        float lo_r[4], ld_r[4];
        m4_mul_vec4(lo_r, inv, lo4);
        m4_mul_vec4(ld_r, inv, ld4);
        if (fabsf(lo_r[3]) < 1e-9f) continue;
        float lo_l[3] = { lo_r[0] / lo_r[3], lo_r[1] / lo_r[3],
                          lo_r[2] / lo_r[3] };
        float ld_l[3] = { ld_r[0], ld_r[1], ld_r[2] };
        v3_norm(ld_l, ld_l);

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

    int node = sdge_editor_selected_node(ed);
    if (node < 0) { ed->dragging = 0; return; }

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
        ed_world_to_parent(ed, node, delta, local);
        sdg_vec3 t;
        sdg_quat r;
        sdg_vec3 s;
        sdge_model_node_get_transform(ed->model, node, t, r, s);
        t[0] = ed->drag_translation[0] + local[0];
        t[1] = ed->drag_translation[1] + local[1];
        t[2] = ed->drag_translation[2] + local[2];
        sdge_model_node_set_transform(ed->model, node, t, r, s);
        sdge_model_refresh(ed->model);
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
        ed_world_to_parent(ed, node, axis_world, axis_local);
        sdg_quat dq;
        q_from_axis_angle(dq, axis_local, angle);
        sdg_quat nr;
        q_mul(nr, dq, ed->drag_rot);
        q_norm(nr, nr);
        sdg_vec3 t, s;
        sdg_quat r;
        sdge_model_node_get_transform(ed->model, node, t, r, s);
        sdge_model_node_set_transform(ed->model, node, t, nr, s);
        sdge_model_refresh(ed->model);
    } else if (ed->drag_tool == SDGE_EDIT_SCALE) {
        float factor = 1.0f + dx * 0.01f;
        if (factor < 0.05f) factor = 0.05f;
        if (factor > 20.0f) factor = 20.0f;
        sdg_vec3 t, s;
        sdg_quat r;
        sdge_model_node_get_transform(ed->model, node, t, r, s);
        sdg_vec3 ns;
        v3_set(ns, ed->drag_scale[0] * factor,
                    ed->drag_scale[1] * factor,
                    ed->drag_scale[2] * factor);
        sdge_model_node_set_transform(ed->model, node, t, r, ns);
        sdge_model_refresh(ed->model);
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
        int node = sdge_editor_selected_node(ed);
        if (node < 0) return;
        sdg_mat4 world;
        sdge_model_node_world(ed->model, node, world);
        ed->dragging = 1;
        ed->drag_tool = ed->tool;
        ed->drag_mx = ed->last_mx;
        ed->drag_my = ed->last_my;
        v3_set(ed->drag_origin, world[12], world[13], world[14]);
        sdge_model_node_get_transform(ed->model, node, ed->drag_translation,
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
    case '1': ed->tool = SDGE_EDIT_SELECT; break;
    case '2': ed->tool = SDGE_EDIT_MOVE;   break;
    case '3': ed->tool = SDGE_EDIT_ROTATE; break;
    case '4': ed->tool = SDGE_EDIT_SCALE;  break;
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

SDGE_API void sdge_editor_render(sdge_editor *ed) {
    if (!ed) return;
    sdge_camera3d_update(&ed->cam);
    if (ed->model) sdge_model_draw(ed->model, NULL);

    int node = sdge_editor_selected_node(ed);
    if (node < 0) return;

    sdg_mat4 world;
    sdge_model_node_world(ed->model, node, world);
    float origin[3] = { world[12], world[13], world[14] };

    /* the box of the selected node */
    float lo[3], hi[3];
    sdge_model_node_bounds(ed->model, node, lo, hi);
    float center[3], half[3];
    for (int k = 0; k < 3; ++k) {
        center[k] = (lo[k] + hi[k]) * 0.5f;
        half[k] = (hi[k] - lo[k]) * 0.5f;
    }
    sdgr_draw_box_wire(center, half, NULL, COL_SELECT);

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

/* -------------------------------------------------------------- textures */

static const char *ed_tex_find(sdge_editor *ed, int node, int slot) {
    if (!ed) return NULL;
    for (int i = 0; i < ed->tex_ref_count; ++i) {
        if (ed->tex_refs[i].node == node && ed->tex_refs[i].slot == slot)
            return ed->tex_refs[i].path;
    }
    return NULL;
}

static void ed_tex_set(sdge_editor *ed, int node, int slot, const char *path) {
    if (!ed) return;
    for (int i = 0; i < ed->tex_ref_count; ++i) {
        if (ed->tex_refs[i].node == node && ed->tex_refs[i].slot == slot) {
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
                                              sizeof(ed_tex_ref) *
                                              (size_t)cap);
        if (!n) return;
        ed->tex_refs = n;
        ed->tex_ref_cap = cap;
    }
    ed_tex_ref *r = &ed->tex_refs[ed->tex_ref_count++];
    r->node = node;
    r->slot = slot;
    snprintf(r->path, sizeof(r->path), "%s", path);
}

SDGE_API const char *sdge_editor_texture_path(sdge_editor *ed, int node, int slot) {
    return ed_tex_find(ed, node, slot);
}

SDGE_API int sdge_editor_apply_texture(sdge_editor *ed, int item, const char *path,
                              int slot) {
    if (!ed || !ed->model) return -1;
    ed_clear_error(ed);
    if (item < 0 || item >= ed->item_count) {
        ed_error(ed, "nothing is selected");
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

    sdga_image img;
    if (sdga_image_load_rgba(path, &img) != 0) {
        ed_error(ed, "cannot decode the image");
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
    if (!tex) {
        ed_error(ed, "cannot upload the texture");
        return -1;
    }
    int node = ed->items[item].node;
    if (sdge_model_set_node_texture(ed->model, node, slot, tex) != 0) {
        sdgr_free_texture(tex);
        ed_error(ed, "cannot apply the texture");
        return -1;
    }
    ed_tex_set(ed, node, slot, path);
    return 0;
}

SDGE_API int sdge_editor_clear_texture(sdge_editor *ed, int item, int slot) {
    if (!ed || !ed->model) return -1;
    if (item < 0 || item >= ed->item_count) return -1;
    int node = ed->items[item].node;
    ed_tex_set(ed, node, slot, NULL);
    return sdge_model_set_node_texture(ed->model, node, slot, 0);
}
