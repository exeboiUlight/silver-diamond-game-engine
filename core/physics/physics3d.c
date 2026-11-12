/* physics3d.c - Silver Diamond Game Engine :: 3D rigid body physics */
#include "physics3d.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CONTACTS3 512

/* the safety limits: a broken/overlapping configuration can never launch a
 * body into the void */
#define SDPH3_MAX_SPEED   30.0f
#define SDPH3_MAX_ANGVEL  25.0f
/* the penetration allowance and the biggest separation speed of the bias */
#define SDPH3_SLOP        0.01f
#define SDPH3_MAX_BIAS    0.6f

/* --------------------------------------------------------------- vectors */

static float v3_dot(const float a[3], const float b[3]) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void v3_cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

static void v3_norm(float a[3]) {
    float l = sqrtf(v3_dot(a, a));
    if (l > 1e-8f) { a[0] /= l; a[1] /= l; a[2] /= l; }
}

/* a stable tangent for a normal: any unit vector perpendicular to it */
static void v3_tangent(const float n[3], float t[3]) {
    float helper[3] = { 1.0f, 0.0f, 0.0f };
    if (fabsf(n[0]) > fabsf(n[1]) && fabsf(n[0]) > fabsf(n[2])) {
        helper[0] = 0.0f; helper[1] = 1.0f;
    }
    v3_cross(n, helper, t);
    v3_norm(t);
}

/* the orientation matrix of a body from its euler angles */
static void body_basis(const sdph3_body *b, float m[9]) {
    float cx = cosf(b->angle[0]), sx = sinf(b->angle[0]);
    float cy = cosf(b->angle[1]), sy = sinf(b->angle[1]);
    float cz = cosf(b->angle[2]), sz = sinf(b->angle[2]);
    /* Rz * Ry * Rx */
    m[0] = cz * cy;  m[1] = cz * sy * sx - sz * cx;  m[2] = cz * sy * cx + sz * sx;
    m[3] = sz * cy;  m[4] = sz * sy * sx + cz * cx;  m[5] = sz * sy * cx - cz * sx;
    m[6] = -sy;      m[7] = cy * sx;                 m[8] = cy * cx;
}

/* the world space direction of a local axis */
static void basis_axis(const float m[9], int i, float out[3]) {
    out[0] = m[i * 3];
    out[1] = m[i * 3 + 1];
    out[2] = m[i * 3 + 2];
}

/* ------------------------------------------------------------- contacts */

typedef struct {
    sdph3_body *a, *b;
    float n[3];          /* the normal, from a to b */
    float p[3];          /* the contact point */
    float depth;
    float normal_mass, tangent_mass;
    float bias;
    float normal_accum, tangent_accum;
    float ra[3], rb[3];
    float restitution, friction;
    float vn_init;
} contact3;

/* the relative velocities of the contact points (defined with the solver) */
static void contact_vel(const contact3 *c, float dv[3]);

/* ------------------------------------------------------------- the world */

/* the broadphase cache (defined below) */
static void cache_free(sdph3_world *w);

sdph3_world *sdph3_world_create(void) {
    sdph3_world *w = (sdph3_world *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->gravity[2] = -9.81f;
    w->damping = 0.02f;
    w->ang_damping = 0.05f;
    w->iterations = 12;
    return w;
}

void sdph3_world_destroy(sdph3_world *w) {
    if (!w) return;
    cache_free(w);
    free(w->bodies);
    free(w);
}

void sdph3_world_set_gravity(sdph3_world *w, float gx, float gy, float gz) {
    if (!w) return;
    w->gravity[0] = gx; w->gravity[1] = gy; w->gravity[2] = gz;
}

void sdph3_world_set_bounds(sdph3_world *w, int use, float minx, float miny,
                            float minz, float maxx, float maxy, float maxz) {
    if (!w) return;
    w->use_bounds = use;
    w->bounds[0] = minx; w->bounds[1] = miny; w->bounds[2] = minz;
    w->bounds[3] = maxx; w->bounds[4] = maxy; w->bounds[5] = maxz;
}

/* ------------------------------------------------------------ the bodies */

static sdph3_body *body_add(sdph3_world *w) {
    if (!w) return NULL;
    if (w->count >= w->cap) {
        int cap = w->cap ? w->cap * 2 : 32;
        sdph3_body *nb = (sdph3_body *)realloc(w->bodies,
                                                sizeof(sdph3_body) * (size_t)cap);
        if (!nb) return NULL;
        w->bodies = nb;
        w->cap = cap;
    }
    sdph3_body *b = &w->bodies[w->count++];
    memset(b, 0, sizeof(*b));
    b->restitution = 0.2f;
    b->friction = 0.4f;
    return b;
}

sdph3_body *sdph3_add_sphere(sdph3_world *w, float x, float y, float z,
                             float radius, float mass) {
    sdph3_body *b = body_add(w);
    if (!b) return NULL;
    b->shape = SDPH3_SPHERE;
    b->pos[0] = x; b->pos[1] = y; b->pos[2] = z;
    b->radius = radius;
    b->bound = radius;
    if (mass > 0.0f) {
        b->inv_mass = 1.0f / mass;
        b->inv_inertia = 0.4f * b->inv_mass / (radius * radius);
    }
    return b;
}

sdph3_body *sdph3_add_box(sdph3_world *w, float x, float y, float z,
                          float hw, float hh, float hd, float mass) {
    sdph3_body *b = body_add(w);
    if (!b) return NULL;
    b->shape = SDPH3_BOX;
    b->pos[0] = x; b->pos[1] = y; b->pos[2] = z;
    b->half[0] = hw; b->half[1] = hh; b->half[2] = hd;
    b->bound = sqrtf(hw * hw + hh * hh + hd * hd);
    if (mass > 0.0f) {
        b->inv_mass = 1.0f / mass;
        float i = (float)mass * (hw * hw + hh * hh + hd * hd) / 3.0f;
        b->inv_inertia = (i > 0.0f) ? (1.0f / i) : 0.0f;
    }
    return b;
}

void sdph3_remove(sdph3_world *w, const sdph3_body *body) {
    if (!w || !body) return;
    for (int i = 0; i < w->count; ++i) {
        if (&w->bodies[i] != body) continue;
        for (int k = i; k < w->count - 1; ++k)
            w->bodies[k] = w->bodies[k + 1];
        w->count--;
        return;
    }
}

void sdph3_set_velocity(sdph3_body *b, float vx, float vy, float vz) {
    if (!b) return;
    b->vel[0] = vx; b->vel[1] = vy; b->vel[2] = vz;
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph3_apply_impulse(sdph3_body *b, float ix, float iy, float iz,
                         float rx, float ry, float rz) {
    if (!b || b->inv_mass <= 0.0f) return;
    b->vel[0] += ix * b->inv_mass;
    b->vel[1] += iy * b->inv_mass;
    b->vel[2] += iz * b->inv_mass;
    float tx = ry * iz - rz * iy;
    float ty = rz * ix - rx * iz;
    float tz = rx * iy - ry * ix;
    b->angvel[0] += tx * b->inv_inertia;
    b->angvel[1] += ty * b->inv_inertia;
    b->angvel[2] += tz * b->inv_inertia;
    b->sleeping = 0;
}

int sdph3_point_in_body(const sdph3_body *b, float x, float y, float z) {
    if (!b) return 0;
    float dx = x - b->pos[0], dy = y - b->pos[1], dz = z - b->pos[2];
    if (b->shape == SDPH3_SPHERE)
        return dx * dx + dy * dy + dz * dz <= b->radius * b->radius;
    float m[9];
    body_basis(b, m);
    /* the local coordinates */
    float lx = dx * m[0] + dy * m[1] + dz * m[2];
    float ly = dx * m[3] + dy * m[4] + dz * m[5];
    float lz = dx * m[6] + dy * m[7] + dz * m[8];
    return fabsf(lx) <= b->half[0] && fabsf(ly) <= b->half[1] &&
           fabsf(lz) <= b->half[2];
}

int sdph3_query_sphere(sdph3_world *w, float x, float y, float z, float r,
                       sdph3_body **out, int max) {
    if (!w || !out) return 0;
    int n = 0;
    for (int i = 0; i < w->count && n < max; ++i) {
        sdph3_body *b = &w->bodies[i];
        float dx = x - b->pos[0], dy = y - b->pos[1], dz = z - b->pos[2];
        float rr = r + b->bound;
        if (dx * dx + dy * dy + dz * dz <= rr * rr) out[n++] = b;
    }
    return n;
}

float sdph3_energy(const sdph3_world *w) {
    if (!w) return 0.0f;
    float e = 0.0f;
    for (int i = 0; i < w->count; ++i) {
        const sdph3_body *b = &w->bodies[i];
        if (b->inv_mass <= 0.0f) continue;
        e += 0.5f / b->inv_mass *
             (b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1] + b->vel[2] * b->vel[2]);
    }
    return e;
}

/* the axis aligned bounds of a body */
static void body_aabb(const sdph3_body *b, const float *basis, float *out) {
    if (b->shape == SDPH3_SPHERE) {
        out[0] = b->pos[0] - b->radius;
        out[1] = b->pos[1] - b->radius;
        out[2] = b->pos[2] - b->radius;
        out[3] = b->pos[0] + b->radius;
        out[4] = b->pos[1] + b->radius;
        out[5] = b->pos[2] + b->radius;
        return;
    }
    float ex = 0.0f, ey = 0.0f, ez = 0.0f;
    for (int i = 0; i < 3; ++i) {
        ex += fabsf(basis[i * 3]) * b->half[i];
        ey += fabsf(basis[i * 3 + 1]) * b->half[i];
        ez += fabsf(basis[i * 3 + 2]) * b->half[i];
    }
    out[0] = b->pos[0] - ex; out[1] = b->pos[1] - ey; out[2] = b->pos[2] - ez;
    out[3] = b->pos[0] + ex; out[4] = b->pos[1] + ey; out[5] = b->pos[2] + ez;
}

/* the broadphase cache: the AABBs, the orientation matrices, the sorted
 * order and the contact buffer are allocated once and reused */
typedef struct {
    float *aabb;        /* 6 floats per body */
    float *basis;       /* 9 floats per body */
    int   *order;       /* the body indices sorted by minx */
    int    count;
    contact3 *contacts;
    int    contact_cap;
} ph3_cache;

static ph3_cache *cache_get(sdph3_world *w) {
    if (!w->cache) w->cache = calloc(1, sizeof(ph3_cache));
    return (ph3_cache *)w->cache;
}

static void cache_free(sdph3_world *w) {
    ph3_cache *c = (ph3_cache *)w->cache;
    if (!c) return;
    free(c->aabb);
    free(c->basis);
    free(c->order);
    free(c->contacts);
    free(c);
    w->cache = NULL;
}

static int cache_reserve(ph3_cache *c, int count) {
    if (c->count >= count) return 0;
    float *na = (float *)realloc(c->aabb, sizeof(float) * 6u * (size_t)count);
    if (!na) return -1;
    c->aabb = na;
    float *nb = (float *)realloc(c->basis, sizeof(float) * 9u * (size_t)count);
    if (!nb) return -1;
    c->basis = nb;
    int *no = (int *)realloc(c->order, sizeof(int) * (size_t)count);
    if (!no) return -1;
    c->order = no;
    c->count = count;
    return 0;
}

static int cache_reserve_contacts(ph3_cache *c, int count) {
    if (c->contact_cap >= count) return 0;
    contact3 *nc = (contact3 *)realloc(c->contacts,
                                       sizeof(contact3) * (size_t)count);
    if (!nc) return -1;
    c->contacts = nc;
    c->contact_cap = count;
    return 0;
}

static void cache_sort(ph3_cache *c) {
    for (int i = 1; i < c->count; ++i) {
        int idx = c->order[i];
        float key = c->aabb[(size_t)idx * 6u];
        int j = i - 1;
        while (j >= 0 && c->aabb[(size_t)c->order[j] * 6u] > key) {
            c->order[j + 1] = c->order[j];
            --j;
        }
        c->order[j + 1] = idx;
    }
}

/* ------------------------------------------------------ the narrow phase */

static int collide_sphere_sphere(sdph3_body *a, sdph3_body *b, contact3 *c) {
    float d[3] = { b->pos[0] - a->pos[0], b->pos[1] - a->pos[1],
                    b->pos[2] - a->pos[2] };
    float d2 = v3_dot(d, d);
    float r = a->radius + b->radius;
    if (d2 >= r * r) return 0;
    float len = sqrtf(d2);
    c->a = a; c->b = b;
    if (len > 1e-6f) { c->n[0] = d[0] / len; c->n[1] = d[1] / len; c->n[2] = d[2] / len; }
    else { c->n[0] = 0; c->n[1] = 0; c->n[2] = 1; }
    c->depth = r - len;
    c->p[0] = a->pos[0] + c->n[0] * (a->radius - c->depth * 0.5f);
    c->p[1] = a->pos[1] + c->n[1] * (a->radius - c->depth * 0.5f);
    c->p[2] = a->pos[2] + c->n[2] * (a->radius - c->depth * 0.5f);
    return 1;
}

/* the closest point of a box to a world point */
static void box_closest(const sdph3_body *b, const float m[9],
                        const float wp[3], float out[3]) {
    float d[3] = { wp[0] - b->pos[0], wp[1] - b->pos[1], wp[2] - b->pos[2] };
    float l[3];
    for (int i = 0; i < 3; ++i)
        l[i] = d[0] * m[i * 3] + d[1] * m[i * 3 + 1] + d[2] * m[i * 3 + 2];
    for (int i = 0; i < 3; ++i) {
        if (l[i] > b->half[i]) l[i] = b->half[i];
        if (l[i] < -b->half[i]) l[i] = -b->half[i];
    }
    for (int i = 0; i < 3; ++i)
        out[i] = b->pos[i] + l[0] * m[i * 3] + l[1] * m[i * 3 + 1] + l[2] * m[i * 3 + 2];
}

static int collide_sphere_box(sdph3_body *sphere, sdph3_body *box,
                              const float *box_basis, contact3 *c) {
    float cp[3];
    box_closest(box, box_basis, sphere->pos, cp);
    float d[3] = { sphere->pos[0] - cp[0], sphere->pos[1] - cp[1],
                    sphere->pos[2] - cp[2] };
    float d2 = v3_dot(d, d);
    if (d2 >= sphere->radius * sphere->radius) return 0;
    float len = sqrtf(d2);
    c->a = sphere; c->b = box;
    if (len > 1e-6f) { c->n[0] = d[0] / len; c->n[1] = d[1] / len; c->n[2] = d[2] / len; }
    else { c->n[0] = 0; c->n[1] = 0; c->n[2] = 1; }
    c->depth = sphere->radius - len;
    c->p[0] = cp[0]; c->p[1] = cp[1]; c->p[2] = cp[2];
    return 1;
}

/* the support point of a box in a world space direction (the fallback) */
static void box_support(const sdph3_body *b, const float m[9],
                        const float dir[3], float out[3]) {
    out[0] = b->pos[0]; out[1] = b->pos[1]; out[2] = b->pos[2];
    for (int i = 0; i < 3; ++i) {
        float ax[3];
        basis_axis(m, i, ax);
        float s = v3_dot(dir, ax);
        float k = (s > 0.0f) ? b->half[i] : (s < 0.0f ? -b->half[i] : 0.0f);
        out[0] += ax[0] * k;
        out[1] += ax[1] * k;
        out[2] += ax[2] * k;
    }
}

/* the Sutherland-Hodgman clip of a convex polygon by the half space
 * dot(p, normal) <= offset */
static int clip_poly(const float in[][3], int count, const float normal[3],
                     float offset, float out[][3], int max) {
    int m = 0;
    for (int i = 0; i < count; ++i) {
        const float *p = in[i];
        const float *q = in[(i + 1) % count];
        float fp = v3_dot(p, normal) - offset;
        float fq = v3_dot(q, normal) - offset;
        if (fp <= 0.0f) {
            if (m < max) { out[m][0] = p[0]; out[m][1] = p[1]; out[m][2] = p[2]; }
            ++m;
        }
        if ((fp < 0.0f && fq > 0.0f) || (fp > 0.0f && fq < 0.0f)) {
            float t = fp / (fp - fq);
            if (m < max) {
                out[m][0] = p[0] + (q[0] - p[0]) * t;
                out[m][1] = p[1] + (q[1] - p[1]) * t;
                out[m][2] = p[2] + (q[2] - p[2]) * t;
            }
            ++m;
        }
    }
    /* the caller must never read past the buffer, even for a degenerate
     * polygon that would need more vertices than the array can hold */
    return (m > max) ? max : m;
}

/* the face contact manifold: the incident face of one box is clipped against
 * the four side planes of the reference face.  The resulting points lie
 * inside the actual overlap area, so the lever arms stay physically correct
 * (a large floor no longer puts the contact point at its centre). */
static int box_manifold(const sdph3_body *a, const sdph3_body *b,
                        const float *ma, const float *mb,
                        const float n[3], int ref_is_b, int axis,
                        float depth, contact3 *out) {
    const sdph3_body *ref = ref_is_b ? b : a;
    const float *mr = ref_is_b ? mb : ma;
    const sdph3_body *inc = ref_is_b ? a : b;
    const float *mi = ref_is_b ? ma : mb;
    float fn[3] = { ref_is_b ? -n[0] : n[0],
                    ref_is_b ? -n[1] : n[1],
                    ref_is_b ? -n[2] : n[2] };
    float half_ref = ref->half[axis];

    /* the incident face is the one most anti-parallel to the reference:
     * pick the local axis most aligned with fn and use its opposite side */
    int jbest = 0;
    float jdot = 0.0f;
    for (int j = 0; j < 3; ++j) {
        float ax[3];
        basis_axis(mi, j, ax);
        float dd = v3_dot(ax, fn);
        if (fabsf(dd) > fabsf(jdot)) { jdot = dd; jbest = j; }
    }
    float iax[3];
    basis_axis(mi, jbest, iax);
    float isign = (jdot > 0.0f) ? -1.0f : 1.0f;
    float ic[3];
    for (int k = 0; k < 3; ++k)
        ic[k] = inc->pos[k] + iax[k] * isign * inc->half[jbest];

    int iu = (jbest + 1) % 3, iv = (jbest + 2) % 3;
    float au[3], av[3];
    basis_axis(mi, iu, au);
    basis_axis(mi, iv, av);

    float poly_a[10][3], poly_b[10][3];
    /* the incident face corners in cyclic order (a "bow tie" order would make
     * the clipped polygon self intersecting and put the centre anywhere) */
    static const int face_su[4] = { -1, 1, 1, -1 };
    static const int face_sv[4] = { -1, -1, 1, 1 };
    int np = 0;
    for (int q = 0; q < 4; ++q) {
        float su = (float)face_su[q];
        float sv = (float)face_sv[q];
        poly_a[np][0] = ic[0] + au[0] * su * inc->half[iu] +
                                 av[0] * sv * inc->half[iv];
        poly_a[np][1] = ic[1] + au[1] * su * inc->half[iu] +
                                 av[1] * sv * inc->half[iv];
        poly_a[np][2] = ic[2] + au[2] * su * inc->half[iu] +
                                 av[2] * sv * inc->half[iv];
        ++np;
    }

    /* clip against the four side planes of the reference face */
    float *src = &poly_a[0][0], *dst = &poly_b[0][0];
    for (int q = 0; q < 3; ++q) {
        if (q == axis) continue;
        float u[3];
        basis_axis(mr, q, u);
        for (int sg = -1; sg <= 1; sg += 2) {
            float nrm[3] = { u[0] * (float)sg, u[1] * (float)sg,
                             u[2] * (float)sg };
            float off = v3_dot(ref->pos, nrm) + ref->half[q];
            np = clip_poly((const float (*)[3])src, np, nrm, off,
                           (float (*)[3])dst, 10);
            float *t = src; src = dst; dst = t;
        }
    }

    /* a single contact at the centre of the clipped overlap: a point at the
     * centre of a flat face applies no tipping torque, while a corner/edge
     * contact still lands on the actual corner (several independent contact
     * points make the sequential solver spin small boxes up) */
    float sum[3] = { 0.0f, 0.0f, 0.0f };
    float max_depth = 0.0f;
    int nc = 0;
    for (int i = 0; i < np; ++i) {
        const float *v = src + (size_t)i * 3u;
        float sep = v3_dot(v, fn) - v3_dot(ref->pos, fn) - half_ref;
        if (sep > 0.0f) continue;
        sum[0] += v[0]; sum[1] += v[1]; sum[2] += v[2];
        if (-sep > max_depth) max_depth = -sep;
        ++nc;
    }
    if (nc > 0) {
        contact3 *c = &out[0];
        c->a = (sdph3_body *)a;
        c->b = (sdph3_body *)b;
        c->n[0] = n[0]; c->n[1] = n[1]; c->n[2] = n[2];
        c->depth = max_depth;
        c->p[0] = sum[0] / (float)nc;
        c->p[1] = sum[1] / (float)nc;
        c->p[2] = sum[2] / (float)nc;
        return 1;
    }

    /* the numerical fallback: a single point between the support points */
    float sa[3], sb[3], neg[3] = { -n[0], -n[1], -n[2] };
    box_support(a, ma, n, sa);
    box_support(b, mb, neg, sb);
    out[0].a = (sdph3_body *)a;
    out[0].b = (sdph3_body *)b;
    out[0].n[0] = n[0]; out[0].n[1] = n[1]; out[0].n[2] = n[2];
    out[0].depth = depth;
    for (int k = 0; k < 3; ++k)
        out[0].p[k] = (sa[k] + sb[k]) * 0.5f;
    return 1;
}

/* the closest points between two box edges (an edge-edge contact) */
static int edge_contact(const sdph3_body *a, const sdph3_body *b,
                        const float *ma, const float *mb,
                        int ea, int eb, const float n[3], float depth,
                        contact3 *out) {
    float d1[3], d2[3];
    basis_axis(ma, ea, d1);
    basis_axis(mb, eb, d2);
    float r[3] = { a->pos[0] - b->pos[0], a->pos[1] - b->pos[1],
                   a->pos[2] - b->pos[2] };
    float aa = v3_dot(d1, d1);
    float ee = v3_dot(d2, d2);
    float bb = v3_dot(d1, d2);
    float cc = v3_dot(d1, r);
    float ff = v3_dot(d2, r);
    float den = aa * ee - bb * bb;
    float s = 0.0f, t = 0.0f;
    if (den > 1e-8f) s = (bb * ff - cc * ee) / den;
    if (s > a->half[ea]) s = a->half[ea];
    if (s < -a->half[ea]) s = -a->half[ea];
    t = (bb * s + ff) / ee;
    if (t > b->half[eb]) t = b->half[eb];
    if (t < -b->half[eb]) t = -b->half[eb];
    s = (bb * t - cc) / aa;
    if (s > a->half[ea]) s = a->half[ea];
    if (s < -a->half[ea]) s = -a->half[ea];

    out->a = (sdph3_body *)a;
    out->b = (sdph3_body *)b;
    out->n[0] = n[0]; out->n[1] = n[1]; out->n[2] = n[2];
    out->depth = depth;
    for (int k = 0; k < 3; ++k)
        out->p[k] = (a->pos[k] + d1[k] * s + b->pos[k] + d2[k] * t) * 0.5f;
    return 1;
}

/* the SAT test for two boxes: returns the number of contact points */
static int collide_box(sdph3_body *a, sdph3_body *b,
                       const float *ma, const float *mb, contact3 *out) {
    float d[3] = { b->pos[0] - a->pos[0], b->pos[1] - a->pos[1],
                    b->pos[2] - a->pos[2] };

    float best = 1e30f;
    float best_axis[3] = { 0, 0, 1 };
    int best_face = 0, best_ref = 0, best_axis_idx = 0;
    int best_edge_a = 0, best_edge_b = 0;

    /* the 6 face axes */
    for (int s = 0; s < 2; ++s) {
        const float *m = s ? mb : ma;
        for (int i = 0; i < 3; ++i) {
            float ax[3] = { m[i * 3], m[i * 3 + 1], m[i * 3 + 2] };
            float ra = 0.0f, rb = 0.0f;
            for (int k = 0; k < 3; ++k) {
                float aa[3], bb[3];
                basis_axis(ma, k, aa);
                basis_axis(mb, k, bb);
                ra += fabsf(v3_dot(ax, aa)) * a->half[k];
                rb += fabsf(v3_dot(ax, bb)) * b->half[k];
            }
            float dist = fabsf(v3_dot(d, ax));
            float overlap = ra + rb - dist;
            if (overlap <= 0.0f) return 0;
            if (overlap < best) {
                best = overlap;
                float sign = (v3_dot(d, ax) < 0.0f) ? -1.0f : 1.0f;
                best_axis[0] = ax[0] * sign;
                best_axis[1] = ax[1] * sign;
                best_axis[2] = ax[2] * sign;
                best_face = 1;
                best_ref = s;
                best_axis_idx = i;
            }
        }
    }

    /* the 9 edge cross products */
    for (int i = 0; i < 3; ++i) {
        float aa[3];
        basis_axis(ma, i, aa);
        for (int k = 0; k < 3; ++k) {
            float bb[3];
            basis_axis(mb, k, bb);
            float ax[3];
            v3_cross(aa, bb, ax);
            float len = sqrtf(v3_dot(ax, ax));
            if (len < 1e-6f) continue;      /* the parallel axes */
            ax[0] /= len; ax[1] /= len; ax[2] /= len;
            float ra = 0.0f, rb = 0.0f;
            for (int q = 0; q < 3; ++q) {
                float qa[3], qb[3];
                basis_axis(ma, q, qa);
                basis_axis(mb, q, qb);
                ra += fabsf(v3_dot(ax, qa)) * a->half[q];
                rb += fabsf(v3_dot(ax, qb)) * b->half[q];
            }
            float dist = fabsf(v3_dot(d, ax));
            float overlap = ra + rb - dist;
            if (overlap <= 0.0f) return 0;
            /* prefer a face contact on a tie: an aligned box pair would
             * otherwise pick a degenerate edge axis and put the contact
             * point on a corner, tipping the body */
            if (overlap < best - 1e-3f) {
                best = overlap;
                float sign = (v3_dot(d, ax) < 0.0f) ? -1.0f : 1.0f;
                best_axis[0] = ax[0] * sign;
                best_axis[1] = ax[1] * sign;
                best_axis[2] = ax[2] * sign;
                best_face = 0;
                best_edge_a = i;
                best_edge_b = k;
            }
        }
    }

    if (best_face)
        return box_manifold(a, b, ma, mb, best_axis, best_ref,
                            best_axis_idx, best, out);
    return edge_contact(a, b, ma, mb, best_edge_a, best_edge_b,
                        best_axis, best, out);
}

static int collide3(sdph3_body *a, sdph3_body *b,
                    const float *ba, const float *bb, contact3 *out) {
    if (a->shape == SDPH3_SPHERE && b->shape == SDPH3_SPHERE)
        return collide_sphere_sphere(a, b, &out[0]) ? 1 : 0;
    if (a->shape == SDPH3_SPHERE) {
        /* the sphere test gives the normal from the box to the sphere, but
         * the convention is "from a to b" -> flip it */
        if (!collide_sphere_box(a, b, bb, &out[0])) return 0;
        out[0].n[0] = -out[0].n[0];
        out[0].n[1] = -out[0].n[1];
        out[0].n[2] = -out[0].n[2];
        out[0].a = a; out[0].b = b;
        return 1;
    }
    if (b->shape == SDPH3_SPHERE) {
        if (!collide_sphere_box(b, a, ba, &out[0])) return 0;
        out[0].a = a; out[0].b = b;
        return 1;
    }
    return collide_box(a, b, ba, bb, out);
}

/* -------------------------------------------------------- the solver */

static void prepare_contact3(contact3 *c, float dt) {
    sdph3_body *a = c->a, *b = c->b;
    for (int i = 0; i < 3; ++i) {
        c->ra[i] = c->p[i] - a->pos[i];
        c->rb[i] = c->p[i] - b->pos[i];
    }
    float cross_a[3], cross_b[3];
    v3_cross(c->ra, c->n, cross_a);
    v3_cross(c->rb, c->n, cross_b);
    float kn = a->inv_mass + b->inv_mass +
               a->inv_inertia * v3_dot(cross_a, cross_a) +
               b->inv_inertia * v3_dot(cross_b, cross_b);
    c->normal_mass = (kn > 0.0f) ? (1.0f / kn) : 0.0f;

    float t[3];
    v3_tangent(c->n, t);
    float ta[3], tb[3];
    v3_cross(c->ra, t, ta);
    v3_cross(c->rb, t, tb);
    float kt = a->inv_mass + b->inv_mass +
               a->inv_inertia * v3_dot(ta, ta) + b->inv_inertia * v3_dot(tb, tb);
    c->tangent_mass = (kt > 0.0f) ? (1.0f / kt) : 0.0f;

    c->restitution = (a->restitution < b->restitution) ? a->restitution
                                                        : b->restitution;
    c->friction = sqrtf(a->friction * b->friction);

    float dv[3];
    contact_vel(c, dv);
    c->vn_init = v3_dot(dv, c->n);
    /* the Baumgarte bias, capped so that a deep overlap (bodies spawned
     * inside each other) cannot inject a huge amount of energy */
    float bias = 0.2f * (c->depth - SDPH3_SLOP) /
                 (dt > 0.0f ? dt : 1.0f);
    if (bias > SDPH3_MAX_BIAS) bias = SDPH3_MAX_BIAS;
    if (bias < 0.0f) bias = 0.0f;
    c->bias = -bias;
    c->normal_accum = 0.0f;
    c->tangent_accum = 0.0f;
}

static void clamp_body_motion(sdph3_body *b) {
    float v2 = b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1] +
               b->vel[2] * b->vel[2];
    if (v2 > SDPH3_MAX_SPEED * SDPH3_MAX_SPEED) {
        float s = SDPH3_MAX_SPEED / sqrtf(v2);
        b->vel[0] *= s; b->vel[1] *= s; b->vel[2] *= s;
    }
    float a2 = b->angvel[0] * b->angvel[0] + b->angvel[1] * b->angvel[1] +
               b->angvel[2] * b->angvel[2];
    if (a2 > SDPH3_MAX_ANGVEL * SDPH3_MAX_ANGVEL) {
        float s = SDPH3_MAX_ANGVEL / sqrtf(a2);
        b->angvel[0] *= s; b->angvel[1] *= s; b->angvel[2] *= s;
    }
}

static void apply_at(sdph3_body *b, const float imp[3], const float r[3]) {
    b->vel[0] += imp[0] * b->inv_mass;
    b->vel[1] += imp[1] * b->inv_mass;
    b->vel[2] += imp[2] * b->inv_mass;
    float tx = r[1] * imp[2] - r[2] * imp[1];
    float ty = r[2] * imp[0] - r[0] * imp[2];
    float tz = r[0] * imp[1] - r[1] * imp[0];
    b->angvel[0] += tx * b->inv_inertia;
    b->angvel[1] += ty * b->inv_inertia;
    b->angvel[2] += tz * b->inv_inertia;
    /* Do NOT clamp here: clamping during the iterations throws away the
     * angular part of an impulse but keeps the linear part, so the solver
     * keeps adding impulses that only translate the body.  That is how a
     * spinning box got launched.  The caps at the end of the step are the
     * safety net. */
}

/* the velocity of a body at a point r relative to its centre of mass:
 * v + w x r */
static void point_vel(const sdph3_body *b, const float r[3], float out[3]) {
    out[0] = b->vel[0] + b->angvel[1] * r[2] - b->angvel[2] * r[1];
    out[1] = b->vel[1] + b->angvel[2] * r[0] - b->angvel[0] * r[2];
    out[2] = b->vel[2] + b->angvel[0] * r[1] - b->angvel[1] * r[0];
}

/* the relative velocity of the two contact points (b minus a) */
static void contact_vel(const contact3 *c, float dv[3]) {
    float pa[3], pb[3];
    point_vel(c->a, c->ra, pa);
    point_vel(c->b, c->rb, pb);
    dv[0] = pb[0] - pa[0];
    dv[1] = pb[1] - pa[1];
    dv[2] = pb[2] - pa[2];
}

static void solve_contact3(contact3 *c) {
    sdph3_body *a = c->a, *b = c->b;
    float dv[3];
    contact_vel(c, dv);
    float vn = v3_dot(dv, c->n);
    float restitution = (c->vn_init < -1.0f) ? c->restitution : 0.0f;

    /* the accumulated impulse is clamped to stay separating: without the
     * clamp the bias keeps pushing a resting stack apart every iteration */
    float lambda = -(vn + restitution * c->vn_init + c->bias) * c->normal_mass;
    float old = c->normal_accum;
    float accum = old + lambda;
    if (accum < 0.0f) accum = 0.0f;
    lambda = accum - old;
    c->normal_accum = accum;
    float imp[3] = { lambda * c->n[0], lambda * c->n[1], lambda * c->n[2] };
    float neg[3] = { -imp[0], -imp[1], -imp[2] };
    apply_at(a, neg, c->ra);
    apply_at(b, imp, c->rb);

    /* the friction (the upper bound uses the accumulated normal impulse) */
    float t[3];
    v3_tangent(c->n, t);
    contact_vel(c, dv);
    float vt = v3_dot(dv, t);
    float lt = -vt * c->tangent_mass;
    float maxf = c->friction * c->normal_accum;
    float oldt = c->tangent_accum;
    float accumt = oldt + lt;
    if (accumt > maxf) accumt = maxf;
    if (accumt < -maxf) accumt = -maxf;
    lt = accumt - oldt;
    c->tangent_accum = accumt;
    float fi[3] = { lt * t[0], lt * t[1], lt * t[2] };
    float fn[3] = { -fi[0], -fi[1], -fi[2] };
    apply_at(a, fn, c->ra);
    apply_at(b, fi, c->rb);
}

/* --------------------------------------------------------- the world step */

void sdph3_step(sdph3_world *w, float dt) {
    if (!w || dt <= 0.0f) return;
    if (w->iterations < 1) w->iterations = 1;

    float damp = 1.0f / (1.0f + w->damping * dt * 60.0f);
    float adamp = 1.0f / (1.0f + w->ang_damping * dt * 60.0f);

    for (int i = 0; i < w->count; ++i) {
        sdph3_body *b = &w->bodies[i];
        if (b->inv_mass <= 0.0f || b->sleeping) continue;
        for (int k = 0; k < 3; ++k) {
            b->vel[k] += w->gravity[k] * dt;
            b->vel[k] *= damp;
            b->angvel[k] *= adamp;
        }
    }
    for (int i = 0; i < w->count; ++i) {
        sdph3_body *b = &w->bodies[i];
        if (b->inv_mass <= 0.0f || b->sleeping) continue;
        for (int k = 0; k < 3; ++k) {
            b->pos[k] += b->vel[k] * dt;
            b->angle[k] += b->angvel[k] * dt;
        }
    }

    ph3_cache *cache = cache_get(w);
    int n = 0;
    contact3 *cs = NULL;
    if (cache && cache_reserve(cache, w->count) == 0 &&
        cache_reserve_contacts(cache, MAX_CONTACTS3) == 0) {
        cs = cache->contacts;
        /* the bounds and the orientation are computed once per step */
        for (int i = 0; i < w->count; ++i) {
            sdph3_body *b = &w->bodies[i];
            float *basis = cache->basis + (size_t)i * 9u;
            body_basis(b, basis);
            body_aabb(b, basis, cache->aabb + (size_t)i * 6u);
            cache->order[i] = i;
        }
        cache_sort(cache);

        for (int si = 0; si < cache->count && n < MAX_CONTACTS3; ++si) {
            int i = cache->order[si];
            sdph3_body *a = &w->bodies[i];
            const float *ba = cache->aabb + (size_t)i * 6u;
            const float *bas = cache->basis + (size_t)i * 9u;
            int a_active = (a->inv_mass > 0.0f) && !a->sleeping;
            for (int sj = si + 1; sj < cache->count && n < MAX_CONTACTS3; ++sj) {
                int k = cache->order[sj];
                const float *bb = cache->aabb + (size_t)k * 6u;
                if (bb[0] > ba[3]) break;          /* the sweep ends here */
                sdph3_body *b = &w->bodies[k];
                int b_active = (b->inv_mass > 0.0f) && !b->sleeping;
                if (!a_active && !b_active) continue;
                if (ba[4] < bb[1] || bb[4] < ba[1]) continue;  /* the Y overlap */
                if (ba[5] < bb[2] || bb[5] < ba[2]) continue;  /* the Z overlap */

                /* a sleeping body must wake up before its contacts are built:
                 * otherwise it receives an impulse but skips the support of
                 * the bodies under it for this step and gets squeezed through
                 * them */
                if (a_active && b->sleeping && b->inv_mass > 0.0f &&
                    (a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] +
                     a->vel[2] * a->vel[2]) > 0.16f) {
                    b->sleeping = 0;
                    b->sleep_timer = 0.0f;
                    b_active = 1;
                }
                if (b_active && a->sleeping && a->inv_mass > 0.0f &&
                    (b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1] +
                     b->vel[2] * b->vel[2]) > 0.16f) {
                    a->sleeping = 0;
                    a->sleep_timer = 0.0f;
                    a_active = 1;
                }
                contact3 tmp[4];
                int nc = collide3(a, b, bas,
                                  cache->basis + (size_t)k * 9u, tmp);
                for (int t = 0; t < nc && n < MAX_CONTACTS3; ++t) {
                    prepare_contact3(&tmp[t], dt);
                    cs[n++] = tmp[t];
                }
            }
        }
    }
    w->contacts = n;

    for (int it = 0; it < w->iterations; ++it)
        for (int i = 0; i < n; ++i)
            solve_contact3(&cs[i]);

    for (int i = 0; i < n; ++i) {
        contact3 *c = &cs[i];
        float total = c->a->inv_mass + c->b->inv_mass;
        if (total <= 0.0f) continue;
        float corr = (c->depth - 0.01f) * 0.4f / total;
        if (corr <= 0.0f) continue;
        /* never teleport a body by more than a fraction of a unit per step */
        if (corr * c->a->inv_mass > 0.2f) corr = 0.2f / c->a->inv_mass;
        if (corr * c->b->inv_mass > 0.2f) corr = 0.2f / c->b->inv_mass;
        for (int k = 0; k < 3; ++k) {
            c->a->pos[k] -= c->n[k] * corr * c->a->inv_mass;
            c->b->pos[k] += c->n[k] * corr * c->b->inv_mass;
        }
    }

    if (w->use_bounds) {
        for (int i = 0; i < w->count; ++i) {
            sdph3_body *b = &w->bodies[i];
            if (b->inv_mass <= 0.0f) continue;
            float r = (b->shape == SDPH3_SPHERE) ? b->radius : b->bound;
            for (int k = 0; k < 3; ++k) {
                if (b->pos[k] - r < w->bounds[k]) {
                    b->pos[k] = w->bounds[k] + r;
                    if (b->vel[k] < 0.0f) b->vel[k] = -b->vel[k] * b->restitution;
                    b->sleeping = 0;
                }
                if (b->pos[k] + r > w->bounds[k + 3]) {
                    b->pos[k] = w->bounds[k + 3] - r;
                    if (b->vel[k] > 0.0f) b->vel[k] = -b->vel[k] * b->restitution;
                    b->sleeping = 0;
                }
            }
        }
    }

    for (int i = 0; i < w->count; ++i) {
        sdph3_body *b = &w->bodies[i];
        if (b->inv_mass <= 0.0f) continue;
        /* the safety caps: an unresolved overlap must not launch the body */
        clamp_body_motion(b);
        float v2 = b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1] +
                   b->vel[2] * b->vel[2];
        float a2 = b->angvel[0] * b->angvel[0] + b->angvel[1] * b->angvel[1] +
                   b->angvel[2] * b->angvel[2];
        if (v2 < 0.25f && a2 < 0.36f) {
            b->sleep_timer += dt;
            if (b->sleep_timer > 0.4f && !b->sleeping) {
                b->sleeping = 1;
                b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
                b->angvel[0] = b->angvel[1] = b->angvel[2] = 0.0f;
            }
        } else {
            b->sleep_timer = 0.0f;
            if (b->sleeping) b->sleeping = 0;
        }
    }

    /* the contact buffer belongs to the cache */
}
