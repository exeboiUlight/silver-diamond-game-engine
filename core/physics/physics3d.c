/* physics3d.c - Silver Diamond Game Engine :: 3D rigid body physics
 *
 * The orientation of a body is a quaternion.  Everything that needs the
 * axes of a box (the SAT test, the support points, the inertia tensor)
 * works with the row major 3x3 matrix built from it:
 *
 *      m[row * 3 + col] = R(row, col)
 *
 * The world direction of the local axis i is the COLUMN i of R:
 *      axis_i = (m[0*3+i], m[1*3+i], m[2*3+i])
 * and the local coordinates of a world vector are R^T * v:
 *      local_i = dot(axis_i, v)
 *
 * Getting this backwards (using the rows as if they were the columns) makes
 * every box collide as if it was mirrored around the diagonal - that is
 * what used to make the bodies spin for no reason.
 */
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

/* two stable tangents for a normal: any orthonormal pair perpendicular to it */
static void v3_tangents(const float n[3], float t1[3], float t2[3]) {
    float helper[3] = { 1.0f, 0.0f, 0.0f };
    if (fabsf(n[0]) > fabsf(n[1]) && fabsf(n[0]) > fabsf(n[2])) {
        helper[0] = 0.0f; helper[1] = 1.0f;
    }
    v3_cross(n, helper, t1);
    v3_norm(t1);
    v3_cross(n, t1, t2);
    v3_norm(t2);
}

/* ----------------------------------------------------------- quaternions */

/* the quaternion -> the row major rotation matrix */
static void quat_to_basis(const float q[4], float m[9]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    float xx = x * x, yy = y * y, zz = z * z;
    float xy = x * y, xz = x * z, yz = y * z;
    float wx = w * x, wy = w * y, wz = w * z;
    m[0] = 1.0f - 2.0f * (yy + zz);
    m[1] = 2.0f * (xy - wz);
    m[2] = 2.0f * (xz + wy);
    m[3] = 2.0f * (xy + wz);
    m[4] = 1.0f - 2.0f * (xx + zz);
    m[5] = 2.0f * (yz - wx);
    m[6] = 2.0f * (xz - wy);
    m[7] = 2.0f * (yz + wx);
    m[8] = 1.0f - 2.0f * (xx + yy);
}

/* the euler angles (Rz*Ry*Rx) -> quaternion */
static void quat_from_euler(float q[4], float ax, float ay, float az) {
    float hx = 0.5f * ax, hy = 0.5f * ay, hz = 0.5f * az;
    float sx = sinf(hx), cx = cosf(hx);
    float sy = sinf(hy), cy = cosf(hy);
    float sz = sinf(hz), cz = cosf(hz);
    q[0] = cz * cy * sx - sz * sy * cx;
    q[1] = cz * sy * cx + sz * cy * sx;
    q[2] = sz * cy * cx - cz * sy * sx;
    q[3] = cz * cy * cx + sz * sy * sx;
}

/* the quaternion -> the euler angles (the same Rz*Ry*Rx convention) */
static void euler_from_quat(float out[3], const float q[4]) {
    float m[9];
    quat_to_basis(q, m);
    float sy = -m[6];                 /* m[2*3+0] = -sin(ay) */
    if (sy > 1.0f) sy = 1.0f;
    if (sy < -1.0f) sy = -1.0f;
    float ay = asinf(sy);
    float ax, az;
    if (fabsf(sy) > 0.99999f) {
        /* the gimbal lock: the X and Z rotations are merged */
        ax = atan2f(-m[1], m[4]);
        az = 0.0f;
    } else {
        ax = atan2f(m[7], m[8]);
        az = atan2f(m[3], m[0]);
    }
    out[0] = ax; out[1] = ay; out[2] = az;
}

static void quat_norm(float q[4]) {
    float l = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (l > 1e-8f) {
        float inv = 1.0f / l;
        q[0] *= inv; q[1] *= inv; q[2] *= inv; q[3] *= inv;
    } else {
        q[0] = 0.0f; q[1] = 0.0f; q[2] = 0.0f; q[3] = 1.0f;
    }
}

/* integrates the orientation with the world space angular velocity:
 *   q' = q + 0.5 * dt * (omega (x) q)
 * The naive "angle += angvel * dt" is wrong as soon as two axes rotate at
 * the same time - the body then spins around a fake axis. */
static void quat_integrate(float q[4], const float w[3], float dt) {
    float dx = 0.5f * dt * w[0];
    float dy = 0.5f * dt * w[1];
    float dz = 0.5f * dt * w[2];
    float nx = dx * q[3] + dy * q[2] - dz * q[1];
    float ny = -dx * q[2] + dy * q[3] + dz * q[0];
    float nz = dx * q[1] - dy * q[0] + dz * q[3];
    float nw = -dx * q[0] - dy * q[1] - dz * q[2];
    q[0] += nx; q[1] += ny; q[2] += nz; q[3] += nw;
    quat_norm(q);
}

/* ------------------------------------------------------------- the basis */

/* the world direction of the local axis i = the column i of the matrix */
static void basis_axis(const float m[9], int i, float out[3]) {
    out[0] = m[0 * 3 + i];
    out[1] = m[1 * 3 + i];
    out[2] = m[2 * 3 + i];
}

/* the local coordinates of a world vector: R^T * v */
static void basis_to_local(const float m[9], const float v[3], float out[3]) {
    for (int i = 0; i < 3; ++i)
        out[i] = v[0] * m[0 * 3 + i] + v[1] * m[1 * 3 + i] + v[2] * m[2 * 3 + i];
}

/* the world vector of a local one: R * v */
static void basis_to_world(const float m[9], const float v[3], float out[3]) {
    for (int i = 0; i < 3; ++i)
        out[i] = v[0] * m[i * 3 + 0] + v[1] * m[i * 3 + 1] + v[2] * m[i * 3 + 2];
}

/* the world space inverse inertia tensor: R * diag(inv) * R^T */
static void body_inv_inertia_world(const sdph3_body *b, const float m[9],
                                   float out[9]) {
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            float s = 0.0f;
            for (int k = 0; k < 3; ++k)
                s += m[i * 3 + k] * b->inv_inertia_axis[k] * m[j * 3 + k];
            out[i * 3 + j] = s;
        }
    }
}

/* ------------------------------------------------------------- contacts */

typedef struct {
    sdph3_body *a, *b;
    const float *iia, *iib;   /* the world inverse inertia of a and b */
    float n[3];          /* the normal, from a to b */
    float p[3];          /* the contact point */
    float depth;
    float normal_mass;
    float tangent_mass[2];
    float bias;
    float normal_accum, tangent_accum[2];
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
    /* the engine is Y-up (the glTF convention), so the gravity pulls
     * down along -Y and not along -Z */
    w->gravity[1] = -9.81f;
    w->damping = 0.05f;
    w->ang_damping = 0.1f;
    w->iterations = 12;
    return w;
}

void sdph3_world_destroy(sdph3_world *w) {
    if (!w) return;
    cache_free(w);
    if (w->bodies) {
        for (int i = 0; i < w->count; ++i) free(w->bodies[i]);
        free(w->bodies);
    }
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

void sdph3_world_set_damping(sdph3_world *w, float linear, float angular) {
    if (!w) return;
    w->damping = linear;
    w->ang_damping = angular;
}

void sdph3_world_set_iterations(sdph3_world *w, int iterations) {
    if (!w) return;
    w->iterations = iterations < 1 ? 1 : iterations;
}

/* ------------------------------------------------------------ the bodies */

/* the bodies are allocated one by one: the array only holds the pointers,
 * so adding a body never moves the others */
static sdph3_body *body_add(sdph3_world *w) {
    if (!w) return NULL;
    if (w->count >= w->cap) {
        int cap = w->cap ? w->cap * 2 : 32;
        sdph3_body **nb = (sdph3_body **)realloc(w->bodies,
                                                 sizeof(sdph3_body *)
                                                 * (size_t)cap);
        if (!nb) return NULL;
        w->bodies = nb;
        w->cap = cap;
    }
    sdph3_body *b = (sdph3_body *)calloc(1, sizeof(*b));
    if (!b) return NULL;
    w->bodies[w->count++] = b;
    b->restitution = 0.2f;
    b->friction = 0.4f;
    b->type = SDPH3_DYNAMIC;
    b->quat[3] = 1.0f;
    return b;
}

/* recomputes the inertia of a body from its shape and mass */
static void body_update_inertia(sdph3_body *b, float mass) {
    if (mass <= 0.0f) {
        b->inv_mass = 0.0f;
        b->inv_inertia = 0.0f;
        b->inv_inertia_axis[0] = 0.0f;
        b->inv_inertia_axis[1] = 0.0f;
        b->inv_inertia_axis[2] = 0.0f;
        return;
    }
    b->inv_mass = 1.0f / mass;
    if (b->shape == SDPH3_SPHERE) {
        /* a solid sphere: I = 2/5 * m * r^2 */
        float r = b->radius > 1e-6f ? b->radius : 1e-6f;
        float i = 0.4f * mass * r * r;
        float inv = (i > 0.0f) ? (1.0f / i) : 0.0f;
        b->inv_inertia_axis[0] = inv;
        b->inv_inertia_axis[1] = inv;
        b->inv_inertia_axis[2] = inv;
    } else {
        /* a box: Ixx = m/3 * (hy^2 + hz^2) and so on */
        float ix = mass * (b->half[1] * b->half[1] + b->half[2] * b->half[2]) / 3.0f;
        float iy = mass * (b->half[0] * b->half[0] + b->half[2] * b->half[2]) / 3.0f;
        float iz = mass * (b->half[0] * b->half[0] + b->half[1] * b->half[1]) / 3.0f;
        b->inv_inertia_axis[0] = (ix > 0.0f) ? (1.0f / ix) : 0.0f;
        b->inv_inertia_axis[1] = (iy > 0.0f) ? (1.0f / iy) : 0.0f;
        b->inv_inertia_axis[2] = (iz > 0.0f) ? (1.0f / iz) : 0.0f;
    }
    b->inv_inertia = (b->inv_inertia_axis[0] + b->inv_inertia_axis[1] +
                      b->inv_inertia_axis[2]) / 3.0f;
}

sdph3_body *sdph3_add_sphere(sdph3_world *w, float x, float y, float z,
                             float radius, float mass) {
    sdph3_body *b = body_add(w);
    if (!b) return NULL;
    b->shape = SDPH3_SPHERE;
    b->pos[0] = x; b->pos[1] = y; b->pos[2] = z;
    b->radius = radius;
    b->bound = radius;
    body_update_inertia(b, mass);
    if (mass <= 0.0f) b->type = SDPH3_STATIC;
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
    body_update_inertia(b, mass);
    if (mass <= 0.0f) b->type = SDPH3_STATIC;
    return b;
}

void sdph3_remove(sdph3_world *w, const sdph3_body *body) {
    if (!w || !body) return;
    for (int i = 0; i < w->count; ++i) {
        if (w->bodies[i] != body) continue;
        free(w->bodies[i]);
        for (int k = i; k < w->count - 1; ++k)
            w->bodies[k] = w->bodies[k + 1];
        w->count--;
        w->bodies[w->count] = NULL;
        return;
    }
}

void sdph3_set_velocity(sdph3_body *b, float vx, float vy, float vz) {
    if (!b) return;
    b->vel[0] = vx; b->vel[1] = vy; b->vel[2] = vz;
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph3_set_angular_velocity(sdph3_body *b, float wx, float wy, float wz) {
    if (!b) return;
    b->angvel[0] = wx; b->angvel[1] = wy; b->angvel[2] = wz;
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph3_set_position(sdph3_body *b, float x, float y, float z) {
    if (!b) return;
    b->pos[0] = x; b->pos[1] = y; b->pos[2] = z;
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph3_set_angle(sdph3_body *b, float ax, float ay, float az) {
    if (!b) return;
    quat_from_euler(b->quat, ax, ay, az);
    b->angle[0] = ax; b->angle[1] = ay; b->angle[2] = az;
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph3_set_quat(sdph3_body *b, const float q[4]) {
    if (!b || !q) return;
    b->quat[0] = q[0]; b->quat[1] = q[1]; b->quat[2] = q[2]; b->quat[3] = q[3];
    quat_norm(b->quat);
    euler_from_quat(b->angle, b->quat);
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph3_get_quat(const sdph3_body *b, float out[4]) {
    if (!b || !out) return;
    out[0] = b->quat[0]; out[1] = b->quat[1];
    out[2] = b->quat[2]; out[3] = b->quat[3];
}

void sdph3_set_type(sdph3_body *b, int type) {
    if (!b) return;
    b->type = type;
    if (type == SDPH3_STATIC) {
        b->inv_mass = 0.0f;
        b->inv_inertia = 0.0f;
        b->inv_inertia_axis[0] = 0.0f;
        b->inv_inertia_axis[1] = 0.0f;
        b->inv_inertia_axis[2] = 0.0f;
    }
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

int sdph3_get_type(const sdph3_body *b) {
    return b ? b->type : SDPH3_STATIC;
}

void sdph3_set_restitution(sdph3_body *b, float restitution) {
    if (b) b->restitution = restitution;
}

void sdph3_set_friction(sdph3_body *b, float friction) {
    if (b) b->friction = friction;
}

void sdph3_wake(sdph3_body *b) {
    if (!b) return;
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph3_sleep(sdph3_body *b) {
    if (!b) return;
    b->sleeping = 1;
    b->sleep_timer = 0.0f;
    b->vel[0] = b->vel[1] = b->vel[2] = 0.0f;
    b->angvel[0] = b->angvel[1] = b->angvel[2] = 0.0f;
}

void sdph3_apply_impulse(sdph3_body *b, float ix, float iy, float iz,
                         float rx, float ry, float rz) {
    if (!b || b->inv_mass <= 0.0f) return;
    b->vel[0] += ix * b->inv_mass;
    b->vel[1] += iy * b->inv_mass;
    b->vel[2] += iz * b->inv_mass;
    float t[3];
    t[0] = ry * iz - rz * iy;
    t[1] = rz * ix - rx * iz;
    t[2] = rx * iy - ry * ix;
    float m[9], ii[9];
    quat_to_basis(b->quat, m);
    body_inv_inertia_world(b, m, ii);
    b->angvel[0] += ii[0] * t[0] + ii[1] * t[1] + ii[2] * t[2];
    b->angvel[1] += ii[3] * t[0] + ii[4] * t[1] + ii[5] * t[2];
    b->angvel[2] += ii[6] * t[0] + ii[7] * t[1] + ii[8] * t[2];
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

int sdph3_point_in_body(const sdph3_body *b, float x, float y, float z) {
    if (!b) return 0;
    float dx = x - b->pos[0], dy = y - b->pos[1], dz = z - b->pos[2];
    if (b->shape == SDPH3_SPHERE)
        return dx * dx + dy * dy + dz * dz <= b->radius * b->radius;
    float m[9], l[3], d[3];
    quat_to_basis(b->quat, m);
    d[0] = dx; d[1] = dy; d[2] = dz;
    basis_to_local(m, d, l);
    return fabsf(l[0]) <= b->half[0] && fabsf(l[1]) <= b->half[1] &&
           fabsf(l[2]) <= b->half[2];
}

int sdph3_query_sphere(sdph3_world *w, float x, float y, float z, float r,
                       sdph3_body **out, int max) {
    if (!w || !out) return 0;
    int n = 0;
    for (int i = 0; i < w->count && n < max; ++i) {
        sdph3_body *b = w->bodies[i];
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
        const sdph3_body *b = w->bodies[i];
        if (b->inv_mass <= 0.0f) continue;
        e += 0.5f / b->inv_mass *
             (b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1] +
              b->vel[2] * b->vel[2]);
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
        float ax[3];
        basis_axis(basis, i, ax);
        ex += fabsf(ax[0]) * b->half[i];
        ey += fabsf(ax[1]) * b->half[i];
        ez += fabsf(ax[2]) * b->half[i];
    }
    out[0] = b->pos[0] - ex; out[1] = b->pos[1] - ey; out[2] = b->pos[2] - ez;
    out[3] = b->pos[0] + ex; out[4] = b->pos[1] + ey; out[5] = b->pos[2] + ez;
}

/* the broadphase cache: the AABBs, the orientation matrices, the sorted
 * order and the contact buffer are allocated once and reused */
typedef struct {
    float *aabb;        /* 6 floats per body */
    float *basis;       /* 9 floats per body */
    float *inertia;     /* 9 floats per body (the world tensor) */
    int   *order;       /* the body indices sorted by minx */
    int    count;       /* the number of live bodies */
    int    cap;         /* the allocated capacity */
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
    free(c->inertia);
    free(c->order);
    free(c->contacts);
    free(c);
    w->cache = NULL;
}

static int cache_reserve(ph3_cache *c, int count) {
    if (c->cap >= count) return 0;
    float *na = (float *)realloc(c->aabb, sizeof(float) * 6u * (size_t)count);
    if (!na) return -1;
    c->aabb = na;
    float *nb = (float *)realloc(c->basis, sizeof(float) * 9u * (size_t)count);
    if (!nb) return -1;
    c->basis = nb;
    float *ni = (float *)realloc(c->inertia, sizeof(float) * 9u * (size_t)count);
    if (!ni) return -1;
    c->inertia = ni;
    int *no = (int *)realloc(c->order, sizeof(int) * (size_t)count);
    if (!no) return -1;
    c->order = no;
    c->cap = count;
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
    basis_to_local(m, d, l);
    for (int i = 0; i < 3; ++i) {
        if (l[i] > b->half[i]) l[i] = b->half[i];
        if (l[i] < -b->half[i]) l[i] = -b->half[i];
    }
    basis_to_world(m, l, out);
    for (int i = 0; i < 3; ++i) out[i] += b->pos[i];
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
 * (a large floor no longer puts the contact point at its centre).
 *
 * Every clipped vertex becomes its own contact point (up to four).  A single
 * point cannot hold a box: it is free to rotate around that point, and the
 * solver then fights itself (the normal impulse rotates the box, the
 * friction reacts, the next normal impulse undoes it - the box ends up
 * spinning and shooting away).  Four points describe the real overlap and
 * keep the box flat. */
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

    /* one contact per penetrating vertex of the clipped polygon */
    int count = 0;
    float base = v3_dot(ref->pos, fn) + half_ref;
    for (int i = 0; i < np && count < 4; ++i) {
        const float *v = src + (size_t)i * 3u;
        float sep = v3_dot(v, fn) - base;
        if (sep > 0.0f) continue;
        contact3 *c = &out[count++];
        c->a = (sdph3_body *)a;
        c->b = (sdph3_body *)b;
        c->n[0] = n[0]; c->n[1] = n[1]; c->n[2] = n[2];
        c->depth = -sep;
        c->p[0] = v[0]; c->p[1] = v[1]; c->p[2] = v[2];
    }
    if (count > 0) return count;

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
            float ax[3];
            basis_axis(m, i, ax);
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

/* the effective mass of a body along a direction at the offset r:
 *   k = 1/m + (r x d)^T * I^-1 * (r x d)
 * The rotational part is what keeps a lever arm from being ignored: without
 * it the solver believes that a push at the corner of a box costs nothing,
 * the impulses grow without a limit and the box is launched. */
static float eff_mass(const sdph3_body *b, const float r[3], const float d[3],
                      const float ii[9]) {
    float rd[3], t[3];
    v3_cross(r, d, rd);
    t[0] = ii[0] * rd[0] + ii[1] * rd[1] + ii[2] * rd[2];
    t[1] = ii[3] * rd[0] + ii[4] * rd[1] + ii[5] * rd[2];
    t[2] = ii[6] * rd[0] + ii[7] * rd[1] + ii[8] * rd[2];
    return b->inv_mass + v3_dot(rd, t);
}

static void prepare_contact3(contact3 *c, float dt,
                             const float iia[9], const float iib[9]) {
    sdph3_body *a = c->a, *b = c->b;
    c->iia = iia;
    c->iib = iib;
    for (int i = 0; i < 3; ++i) {
        c->ra[i] = c->p[i] - a->pos[i];
        c->rb[i] = c->p[i] - b->pos[i];
    }
    c->normal_mass = eff_mass(a, c->ra, c->n, iia) +
                     eff_mass(b, c->rb, c->n, iib);
    if (c->normal_mass > 0.0f) c->normal_mass = 1.0f / c->normal_mass;
    else c->normal_mass = 0.0f;

    float t1[3], t2[3];
    v3_tangents(c->n, t1, t2);
    float m1 = eff_mass(a, c->ra, t1, iia) + eff_mass(b, c->rb, t1, iib);
    float m2 = eff_mass(a, c->ra, t2, iia) + eff_mass(b, c->rb, t2, iib);
    c->tangent_mass[0] = (m1 > 0.0f) ? (1.0f / m1) : 0.0f;
    c->tangent_mass[1] = (m2 > 0.0f) ? (1.0f / m2) : 0.0f;

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
    c->tangent_accum[0] = 0.0f;
    c->tangent_accum[1] = 0.0f;
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

static void apply_at(sdph3_body *b, const float imp[3], const float r[3],
                     const float ii[9]) {
    b->vel[0] += imp[0] * b->inv_mass;
    b->vel[1] += imp[1] * b->inv_mass;
    b->vel[2] += imp[2] * b->inv_mass;
    float t[3];
    v3_cross(r, imp, t);
    b->angvel[0] += ii[0] * t[0] + ii[1] * t[1] + ii[2] * t[2];
    b->angvel[1] += ii[3] * t[0] + ii[4] * t[1] + ii[5] * t[2];
    b->angvel[2] += ii[6] * t[0] + ii[7] * t[1] + ii[8] * t[2];
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

static void solve_contact3(contact3 *c, const float iia[9],
                           const float iib[9]) {
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
    apply_at(a, neg, c->ra, iia);
    apply_at(b, imp, c->rb, iib);
}

/* the friction of one contact: BOTH tangents are solved (a single tangent
 * lets the body slide sideways and turn around the free axis), and the
 * accumulated impulse is clamped to the friction circle of the normal
 * impulse - without the clamp the friction can grow without a limit and
 * inject energy. */
static void solve_friction3(contact3 *c, const float iia[9],
                            const float iib[9]) {
    sdph3_body *a = c->a, *b = c->b;
    float t1[3], t2[3];
    v3_tangents(c->n, t1, t2);
    float dv[3];
    contact_vel(c, dv);
    float v1 = v3_dot(dv, t1);
    float v2 = v3_dot(dv, t2);
    float l1 = -v1 * c->tangent_mass[0];
    float l2 = -v2 * c->tangent_mass[1];
    float maxf = c->friction * c->normal_accum;
    float n1 = c->tangent_accum[0] + l1;
    float n2 = c->tangent_accum[1] + l2;
    float mag = sqrtf(n1 * n1 + n2 * n2);
    if (mag > maxf && mag > 1e-8f) {
        float s = maxf / mag;
        n1 *= s;
        n2 *= s;
    }
    l1 = n1 - c->tangent_accum[0];
    l2 = n2 - c->tangent_accum[1];
    c->tangent_accum[0] = n1;
    c->tangent_accum[1] = n2;

    float fi[3];
    for (int k = 0; k < 3; ++k)
        fi[k] = l1 * t1[k] + l2 * t2[k];
    float fn[3] = { -fi[0], -fi[1], -fi[2] };
    apply_at(a, fn, c->ra, iia);
    apply_at(b, fi, c->rb, iib);
}

/* --------------------------------------------------------- the world step */

void sdph3_step(sdph3_world *w, float dt) {
    if (!w || dt <= 0.0f) return;
    if (w->iterations < 1) w->iterations = 1;

    /* the damping is frame rate independent: the factor is the fraction of
     * the velocity that is lost per second (a damping of 0.05 loses 5% of
     * the speed in one second, no matter how many frames there are) */
    float damp = 1.0f / (1.0f + w->damping * dt);
    float adamp = 1.0f / (1.0f + w->ang_damping * dt);

    /* 1: the external forces */
    for (int i = 0; i < w->count; ++i) {
        sdph3_body *b = w->bodies[i];
        if (b->type != SDPH3_DYNAMIC) continue;
        if (b->inv_mass <= 0.0f || b->sleeping) continue;
        float ld = (b->linear_damping > 0.0f)
                   ? 1.0f / (1.0f + b->linear_damping * dt) : damp;
        float ad = (b->angular_damping > 0.0f)
                   ? 1.0f / (1.0f + b->angular_damping * dt) : adamp;
        for (int k = 0; k < 3; ++k) {
            b->vel[k] += w->gravity[k] * dt;
            b->vel[k] *= ld;
            b->angvel[k] *= ad;
        }
    }

    /* 2: the integration of the positions and the orientations */
    for (int i = 0; i < w->count; ++i) {
        sdph3_body *b = w->bodies[i];
        if (b->type != SDPH3_DYNAMIC) continue;
        if (b->inv_mass <= 0.0f || b->sleeping) continue;
        for (int k = 0; k < 3; ++k)
            b->pos[k] += b->vel[k] * dt;
        quat_integrate(b->quat, b->angvel, dt);
        euler_from_quat(b->angle, b->quat);
    }

    ph3_cache *cache = cache_get(w);
    int n = 0;
    contact3 *cs = NULL;
    if (cache && cache_reserve(cache, w->count) == 0 &&
        cache_reserve_contacts(cache, MAX_CONTACTS3) == 0) {
        cs = cache->contacts;
        cache->count = w->count;
        /* the bounds, the orientation and the inertia are computed once */
        for (int i = 0; i < w->count; ++i) {
            sdph3_body *b = w->bodies[i];
            float *basis = cache->basis + (size_t)i * 9u;
            quat_to_basis(b->quat, basis);
            body_inv_inertia_world(b, basis, cache->inertia + (size_t)i * 9u);
            body_aabb(b, basis, cache->aabb + (size_t)i * 6u);
            cache->order[i] = i;
        }
        cache_sort(cache);

        for (int si = 0; si < cache->count && n < MAX_CONTACTS3; ++si) {
            int i = cache->order[si];
            sdph3_body *a = w->bodies[i];
            const float *ba = cache->aabb + (size_t)i * 6u;
            const float *bas = cache->basis + (size_t)i * 9u;
            const float *bia = cache->inertia + (size_t)i * 9u;
            int a_active = (a->type == SDPH3_DYNAMIC) && !a->sleeping;
            for (int sj = si + 1; sj < cache->count && n < MAX_CONTACTS3; ++sj) {
                int k = cache->order[sj];
                const float *bb = cache->aabb + (size_t)k * 6u;
                if (bb[0] > ba[3]) break;          /* the sweep ends here */
                sdph3_body *b = w->bodies[k];
                const float *bib = cache->inertia + (size_t)k * 9u;
                int b_active = (b->type == SDPH3_DYNAMIC) && !b->sleeping;
                if (!a_active && !b_active) continue;
                if (ba[4] < bb[1] || bb[4] < ba[1]) continue;  /* the Y overlap */
                if (ba[5] < bb[2] || bb[5] < ba[2]) continue;  /* the Z overlap */

                /* a sleeping body must wake up before its contacts are built:
                 * otherwise it receives an impulse but skips the support of
                 * the bodies under it for this step and gets squeezed through
                 * them */
                if (a_active && b->sleeping && b->type == SDPH3_DYNAMIC &&
                    (a->vel[0] * a->vel[0] + a->vel[1] * a->vel[1] +
                     a->vel[2] * a->vel[2]) > 0.16f) {
                    b->sleeping = 0;
                    b->sleep_timer = 0.0f;
                    b_active = 1;
                }
                if (b_active && a->sleeping && a->type == SDPH3_DYNAMIC &&
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
                    prepare_contact3(&tmp[t], dt, bia, bib);
                    cs[n++] = tmp[t];
                }
            }
        }
    }
    w->contacts = n;

    /* 3: the solver - all the normal impulses first, then all the friction
     * ones.  Solving them together lets the rotation caused by one contact
     * fight the friction of the next one and the box starts to buzz. */
    for (int it = 0; it < w->iterations; ++it) {
        for (int i = 0; i < n; ++i)
            solve_contact3(&cs[i], cs[i].iia, cs[i].iib);
        for (int i = 0; i < n; ++i)
            solve_friction3(&cs[i], cs[i].iia, cs[i].iib);
    }

    /* 4: the positional correction (so the bodies do not sink) */
    for (int i = 0; i < n; ++i) {
        contact3 *c = &cs[i];
        float total = c->a->inv_mass + c->b->inv_mass;
        if (total <= 0.0f) continue;
        float corr = (c->depth - SDPH3_SLOP) * 0.4f / total;
        if (corr <= 0.0f) continue;
        /* never teleport a body by more than a fraction of a unit per step */
        if (corr * c->a->inv_mass > 0.2f) corr = 0.2f / c->a->inv_mass;
        if (corr * c->b->inv_mass > 0.2f) corr = 0.2f / c->b->inv_mass;
        for (int k = 0; k < 3; ++k) {
            c->a->pos[k] -= c->n[k] * corr * c->a->inv_mass;
            c->b->pos[k] += c->n[k] * corr * c->b->inv_mass;
        }
    }

    /* 5: the world bounds */
    if (w->use_bounds) {
        for (int i = 0; i < w->count; ++i) {
            sdph3_body *b = w->bodies[i];
            if (b->inv_mass <= 0.0f) continue;
            /* the half extents of the body along the world axes */
            float ext[3];
            if (b->shape == SDPH3_SPHERE) {
                ext[0] = ext[1] = ext[2] = b->radius;
            } else {
                float m[9];
                quat_to_basis(b->quat, m);
                for (int k = 0; k < 3; ++k) {
                    float s = 0.0f;
                    for (int q = 0; q < 3; ++q) {
                        float ax[3];
                        basis_axis(m, q, ax);
                        s += fabsf(ax[k]) * b->half[q];
                    }
                    ext[k] = s;
                }
            }
            for (int k = 0; k < 3; ++k) {
                if (b->pos[k] - ext[k] < w->bounds[k]) {
                    b->pos[k] = w->bounds[k] + ext[k];
                    if (b->vel[k] < 0.0f) b->vel[k] = -b->vel[k] * b->restitution;
                    b->sleeping = 0;
                }
                if (b->pos[k] + ext[k] > w->bounds[k + 3]) {
                    b->pos[k] = w->bounds[k + 3] - ext[k];
                    if (b->vel[k] > 0.0f) b->vel[k] = -b->vel[k] * b->restitution;
                    b->sleeping = 0;
                }
            }
        }
    }

    /* 6: the sleeping */
    for (int i = 0; i < w->count; ++i) {
        sdph3_body *b = w->bodies[i];
        if (b->type != SDPH3_DYNAMIC) continue;
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
