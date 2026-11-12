/* physics2d.c - Silver Diamond Game Engine :: 2D rigid body physics */
#include "physics2d.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

#define MAX_CONTACTS 512

/* --------------------------------------------------------------- vectors */

static float v2_dot(float ax, float ay, float bx, float by) {
    return ax * bx + ay * by;
}

/* ------------------------------------------------------------- contacts */

typedef struct {
    sdph2_body *a, *b;
    float nx, ny;       /* the normal, from a to b */
    float px, py;       /* the contact point (world space) */
    float depth;        /* the penetration */
    float normal_mass;  /* the effective mass along the normal */
    float tangent_mass;
    float bias;         /* the Baumgarte bias */
    float ra[2], rb[2]; /* the offsets from the centres */
    float restitution;
    float friction;
    float vn_init;
} contact;

/* ------------------------------------------------------------- the world */

/* the broadphase cache (defined below) */
static void cache_free(sdph2_world *w);

sdph2_world *sdph2_world_create(void) {
    sdph2_world *w = (sdph2_world *)calloc(1, sizeof(*w));
    if (!w) return NULL;
    w->gravity[1] = -9.81f;
    w->damping = 0.02f;
    w->ang_damping = 0.05f;
    w->iterations = 8;
    return w;
}

void sdph2_world_destroy(sdph2_world *w) {
    if (!w) return;
    cache_free(w);
    free(w->bodies);
    free(w);
}

void sdph2_world_set_gravity(sdph2_world *w, float gx, float gy) {
    if (!w) return;
    w->gravity[0] = gx;
    w->gravity[1] = gy;
}

void sdph2_world_set_bounds(sdph2_world *w, int use, float minx, float miny,
                            float maxx, float maxy) {
    if (!w) return;
    w->use_bounds = use;
    w->bounds[0] = minx; w->bounds[1] = miny;
    w->bounds[2] = maxx; w->bounds[3] = maxy;
}

/* ------------------------------------------------------------ the bodies */

static sdph2_body *body_add(sdph2_world *w) {
    if (!w) return NULL;
    if (w->count >= w->cap) {
        int cap = w->cap ? w->cap * 2 : 32;
        sdph2_body *nb = (sdph2_body *)realloc(w->bodies,
                                               sizeof(sdph2_body) * (size_t)cap);
        if (!nb) return NULL;
        w->bodies = nb;
        w->cap = cap;
    }
    sdph2_body *b = &w->bodies[w->count++];
    memset(b, 0, sizeof(*b));
    b->restitution = 0.2f;
    b->friction = 0.4f;
    b->inv_inertia = 0.0f;
    return b;
}

sdph2_body *sdph2_add_circle(sdph2_world *w, float x, float y, float radius,
                             float mass) {
    sdph2_body *b = body_add(w);
    if (!b) return NULL;
    b->shape = SDPH2_CIRCLE;
    b->pos[0] = x; b->pos[1] = y;
    b->radius = radius;
    b->bound = radius;
    if (mass > 0.0f) {
        b->inv_mass = 1.0f / mass;
        b->inv_inertia = 0.5f * b->inv_mass / (radius * radius);
    }
    return b;
}

sdph2_body *sdph2_add_box(sdph2_world *w, float x, float y, float hw,
                          float hh, float mass, float angle) {
    sdph2_body *b = body_add(w);
    if (!b) return NULL;
    b->shape = SDPH2_BOX;
    b->pos[0] = x; b->pos[1] = y;
    b->half[0] = hw; b->half[1] = hh;
    b->angle = angle;
    b->bound = sqrtf(hw * hw + hh * hh);
    if (mass > 0.0f) {
        b->inv_mass = 1.0f / mass;
        float i = (float)(mass) * (hw * hw + hh * hh) / 3.0f;   /* 2D box */
        b->inv_inertia = (i > 0.0f) ? (1.0f / i) : 0.0f;
    }
    return b;
}

void sdph2_remove(sdph2_world *w, const sdph2_body *body) {
    if (!w || !body) return;
    for (int i = 0; i < w->count; ++i) {
        if (&w->bodies[i] != body) continue;
        for (int k = i; k < w->count - 1; ++k)
            w->bodies[k] = w->bodies[k + 1];
        w->count--;
        return;
    }
}

void sdph2_set_velocity(sdph2_body *b, float vx, float vy) {
    if (!b) return;
    b->vel[0] = vx;
    b->vel[1] = vy;
    b->sleeping = 0;
    b->sleep_timer = 0.0f;
}

void sdph2_apply_impulse(sdph2_body *b, float ix, float iy, float rx, float ry) {
    if (!b || b->inv_mass <= 0.0f) return;
    b->vel[0] += ix * b->inv_mass;
    b->vel[1] += iy * b->inv_mass;
    b->angvel += (rx * iy - ry * ix) * b->inv_inertia;
    b->sleeping = 0;
}

int sdph2_point_in_body(const sdph2_body *b, float x, float y) {
    if (!b) return 0;
    float dx = x - b->pos[0], dy = y - b->pos[1];
    if (b->shape == SDPH2_CIRCLE)
        return dx * dx + dy * dy <= b->radius * b->radius;
    float c = cosf(-b->angle), s = sinf(-b->angle);
    float lx = dx * c - dy * s;
    float ly = dx * s + dy * c;
    return fabsf(lx) <= b->half[0] && fabsf(ly) <= b->half[1];
}

int sdph2_query_circle(sdph2_world *w, float x, float y, float r,
                        sdph2_body **out, int max) {
    if (!w || !out) return 0;
    int n = 0;
    for (int i = 0; i < w->count && n < max; ++i) {
        sdph2_body *b = &w->bodies[i];
        float dx = x - b->pos[0], dy = y - b->pos[1];
        float rr = r + b->bound;
        if (dx * dx + dy * dy <= rr * rr) out[n++] = b;
    }
    return n;
}

float sdph2_energy(const sdph2_world *w) {
    if (!w) return 0.0f;
    float e = 0.0f;
    for (int i = 0; i < w->count; ++i) {
        const sdph2_body *b = &w->bodies[i];
        e += 0.5f * b->inv_mass > 0.0f
           ? 0.5f / b->inv_mass * (b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1])
           : 0.0f;
    }
    return e;
}

/* ------------------------------------------------------ the broadphase */

/* the axis aligned bounds of a body */
static void body_aabb(const sdph2_body *b, float *out) {
    if (b->shape == SDPH2_CIRCLE) {
        out[0] = b->pos[0] - b->radius;
        out[1] = b->pos[1] - b->radius;
        out[2] = b->pos[0] + b->radius;
        out[3] = b->pos[1] + b->radius;
        return;
    }
    float c = cosf(b->angle), s = sinf(b->angle);
    float ex = fabsf(c) * b->half[0] + fabsf(s) * b->half[1];
    float ey = fabsf(s) * b->half[0] + fabsf(c) * b->half[1];
    out[0] = b->pos[0] - ex;
    out[1] = b->pos[1] - ey;
    out[2] = b->pos[0] + ex;
    out[3] = b->pos[1] + ey;
}

/* the broadphase cache: the AABBs, the sorted order and the contact
 * buffer are allocated once and reused - a step never mallocs */
typedef struct {
    float *aabb;        /* 4 floats per body */
    int   *order;       /* the body indices sorted by minx */
    int    count;
    contact *contacts;
    int    contact_cap;
} ph2_cache;

static ph2_cache *cache_get(sdph2_world *w) {
    if (!w->cache) w->cache = calloc(1, sizeof(ph2_cache));
    return (ph2_cache *)w->cache;
}

static void cache_free(sdph2_world *w) {
    ph2_cache *c = (ph2_cache *)w->cache;
    if (!c) return;
    free(c->aabb);
    free(c->order);
    free(c->contacts);
    free(c);
    w->cache = NULL;
}

static int cache_reserve(ph2_cache *c, int count) {
    if (c->count >= count) return 0;
    float *na = (float *)realloc(c->aabb, sizeof(float) * 4u * (size_t)count);
    if (!na) return -1;
    c->aabb = na;
    int *no = (int *)realloc(c->order, sizeof(int) * (size_t)count);
    if (!no) return -1;
    c->order = no;
    c->count = count;
    return 0;
}

static int cache_reserve_contacts(ph2_cache *c, int count) {
    if (c->contact_cap >= count) return 0;
    contact *nc = (contact *)realloc(c->contacts, sizeof(contact) * (size_t)count);
    if (!nc) return -1;
    c->contacts = nc;
    c->contact_cap = count;
    return 0;
}

/* the insertion sort by minx (the body counts are small) */
static void cache_sort(ph2_cache *c) {
    for (int i = 1; i < c->count; ++i) {
        int idx = c->order[i];
        float key = c->aabb[(size_t)idx * 4u];
        int j = i - 1;
        while (j >= 0 && c->aabb[(size_t)c->order[j] * 4u] > key) {
            c->order[j + 1] = c->order[j];
            --j;
        }
        c->order[j + 1] = idx;
    }
}

/* ----------------------------------------------------- the narrow phase */

/* the closest point of a box to a world point (local space) */
static void box_closest(const sdph2_body *b, float wx, float wy,
                        float *lx, float *ly) {
    float c = cosf(-b->angle), s = sinf(-b->angle);
    float dx = wx - b->pos[0], dy = wy - b->pos[1];
    *lx = dx * c - dy * s;
    *ly = dx * s + dy * c;
    if (*lx > b->half[0]) *lx = b->half[0];
    if (*lx < -b->half[0]) *lx = -b->half[0];
    if (*ly > b->half[1]) *ly = b->half[1];
    if (*ly < -b->half[1]) *ly = -b->half[1];
}

/* the collision between two circles */
static int collide_circle(sdph2_body *a, sdph2_body *b, contact *c) {
    float dx = b->pos[0] - a->pos[0];
    float dy = b->pos[1] - a->pos[1];
    float d2 = dx * dx + dy * dy;
    float r = a->radius + b->radius;
    if (d2 >= r * r) return 0;
    float d = sqrtf(d2);
    c->a = a; c->b = b;
    if (d > 1e-6f) { c->nx = dx / d; c->ny = dy / d; }
    else { c->nx = 0.0f; c->ny = 1.0f; }
    c->depth = r - d;
    c->px = a->pos[0] + c->nx * (a->radius - c->depth * 0.5f);
    c->py = a->pos[1] + c->ny * (a->radius - c->depth * 0.5f);
    return 1;
}

/* the collision between a circle and a box */
static int collide_circle_box(sdph2_body *circle, sdph2_body *box,
                               contact *c) {
    float lx, ly;
    box_closest(box, circle->pos[0], circle->pos[1], &lx, &ly);
    float bc = cosf(box->angle), bs = sinf(box->angle);
    float wx = box->pos[0] + lx * bc - ly * bs;
    float wy = box->pos[1] + lx * bs + ly * bc;
    float dx = circle->pos[0] - wx;
    float dy = circle->pos[1] - wy;
    float d2 = dx * dx + dy * dy;
    if (d2 >= circle->radius * circle->radius) return 0;
    float d = sqrtf(d2);
    c->a = circle; c->b = box;
    if (d > 1e-6f) { c->nx = dx / d; c->ny = dy / d; }
    else { c->nx = 0.0f; c->ny = 1.0f; }
    c->depth = circle->radius - d;
    c->px = wx; c->py = wy;
    return 1;
}

/* the collision between two boxes (SAT) */
static int collide_box(sdph2_body *a, sdph2_body *b, contact *c) {
    float axes[4][2];
    float ac = cosf(a->angle), as = sinf(a->angle);
    float bc = cosf(b->angle), bs = sinf(b->angle);
    axes[0][0] = ac; axes[0][1] = as;
    axes[1][0] = -as; axes[1][1] = ac;
    axes[2][0] = bc; axes[2][1] = bs;
    axes[3][0] = -bs; axes[3][1] = bc;

    float dx = b->pos[0] - a->pos[0];
    float dy = b->pos[1] - a->pos[1];

    float best_overlap = 1e30f;
    int best = -1;
    for (int i = 0; i < 4; ++i) {
        float ax = axes[i][0], ay = axes[i][1];
        /* the projection radius of a and b */
        float ra = fabsf(v2_dot(ax, ay, ac, as)) * a->half[0] +
                   fabsf(v2_dot(ax, ay, -as, ac)) * a->half[1];
        float rb = fabsf(v2_dot(ax, ay, bc, bs)) * b->half[0] +
                   fabsf(v2_dot(ax, ay, -bs, bc)) * b->half[1];
        float dist = fabsf(v2_dot(dx, dy, ax, ay));
        float overlap = ra + rb - dist;
        if (overlap <= 0.0f) return 0;          /* separated */
        if (overlap < best_overlap) {
            best_overlap = overlap;
            best = i;
        }
    }
    if (best < 0) return 0;

    float ax = axes[best][0], ay = axes[best][1];
    float sign = (v2_dot(dx, dy, ax, ay) < 0.0f) ? -1.0f : 1.0f;
    c->a = a; c->b = b;
    c->nx = ax * sign;
    c->ny = ay * sign;
    c->depth = best_overlap;

    /* the contact point: the deepest corner of b along the normal */
    float corners[4][2] = {
        {  b->half[0],  b->half[1] },
        { -b->half[0],  b->half[1] },
        {  b->half[0], -b->half[1] },
        { -b->half[0], -b->half[1] },
    };
    float bestd = 1e30f;
    float bx = b->pos[0], by = b->pos[1];
    for (int i = 0; i < 4; ++i) {
        float lx = corners[i][0], ly = corners[i][1];
        float wx = b->pos[0] + lx * bc - ly * bs;
        float wy = b->pos[1] + lx * bs + ly * bc;
        float proj = (wx - a->pos[0]) * c->nx + (wy - a->pos[1]) * c->ny;
        if (proj < bestd) { bestd = proj; bx = wx; by = wy; }
    }
    c->px = bx; c->py = by;
    return 1;
}

static int collide(sdph2_body *a, sdph2_body *b, contact *c) {
    if (a->shape == SDPH2_CIRCLE && b->shape == SDPH2_CIRCLE)
        return collide_circle(a, b, c);
    if (a->shape == SDPH2_CIRCLE) {
        /* the circle test gives the normal from the box to the circle, but
         * the convention is "from a to b" -> flip it */
        if (!collide_circle_box(a, b, c)) return 0;
        c->nx = -c->nx; c->ny = -c->ny;
        c->a = a; c->b = b;
        return 1;
    }
    if (b->shape == SDPH2_CIRCLE) {
        if (!collide_circle_box(b, a, c)) return 0;
        c->a = a; c->b = b;
        return 1;
    }
    return collide_box(a, b, c);
}

/* -------------------------------------------------------- the solver */

static void prepare_contact(contact *c, float dt) {
    sdph2_body *a = c->a, *b = c->b;
    c->ra[0] = c->px - a->pos[0];
    c->ra[1] = c->py - a->pos[1];
    c->rb[0] = c->px - b->pos[0];
    c->rb[1] = c->py - b->pos[1];

    float rn_a = c->ra[0] * c->ny - c->ra[1] * c->nx;
    float rn_b = c->rb[0] * c->ny - c->rb[1] * c->nx;
    float kn = a->inv_mass + b->inv_mass +
               a->inv_inertia * rn_a * rn_a + b->inv_inertia * rn_b * rn_b;
    c->normal_mass = (kn > 0.0f) ? (1.0f / kn) : 0.0f;

    float tx = -c->ny, ty = c->nx;
    float rt_a = c->ra[0] * ty - c->ra[1] * tx;
    float rt_b = c->rb[0] * ty - c->rb[1] * tx;
    float kt = a->inv_mass + b->inv_mass +
               a->inv_inertia * rt_a * rt_a + b->inv_inertia * rt_b * rt_b;
    c->tangent_mass = (kt > 0.0f) ? (1.0f / kt) : 0.0f;

    c->restitution = (a->restitution < b->restitution) ? a->restitution
                                                        : b->restitution;
    c->friction = sqrtf(a->friction * b->friction);

    /* the relative velocity along the normal */
    float dvx = (b->vel[0] - b->angvel * c->rb[1]) -
                (a->vel[0] - a->angvel * c->ra[1]);
    float dvy = (b->vel[1] + b->angvel * c->rb[0]) -
                (a->vel[1] + a->angvel * c->ra[0]);
    c->vn_init = dvx * c->nx + dvy * c->ny;

    /* the Baumgarte bias (the penetration correction) */
    c->bias = -0.2f * c->depth / (dt > 0.0f ? dt : 1.0f);
}

static void solve_contact(contact *c) {
    sdph2_body *a = c->a, *b = c->b;

    /* the normal impulse */
    float dvx = (b->vel[0] - b->angvel * c->rb[1]) -
                (a->vel[0] - a->angvel * c->ra[1]);
    float dvy = (b->vel[1] + b->angvel * c->rb[0]) -
                (a->vel[1] + a->angvel * c->ra[0]);
    float vn = dvx * c->nx + dvy * c->ny;

    float restitution = (c->vn_init < -1.0f) ? c->restitution : 0.0f;
    /* the target velocity is -e * vn_init (separating), so the impulse is
     *   J = (target - vn) / (1/ma + 1/mb) */
    float lambda = -(vn + restitution * c->vn_init + c->bias) * c->normal_mass;
    if (lambda < 0.0f && vn > 0.0f) lambda = 0.0f;

    float ix = lambda * c->nx, iy = lambda * c->ny;
    a->vel[0] -= ix * a->inv_mass;
    a->vel[1] -= iy * a->inv_mass;
    a->angvel -= (c->ra[0] * iy - c->ra[1] * ix) * a->inv_inertia;
    b->vel[0] += ix * b->inv_mass;
    b->vel[1] += iy * b->inv_mass;
    b->angvel += (c->rb[0] * iy - c->rb[1] * ix) * b->inv_inertia;

    /* the friction impulse */
    float tx = -c->ny, ty = c->nx;
    dvx = (b->vel[0] - b->angvel * c->rb[1]) - (a->vel[0] - a->angvel * c->ra[1]);
    dvy = (b->vel[1] + b->angvel * c->rb[0]) - (a->vel[1] + a->angvel * c->ra[0]);
    float vt = dvx * tx + dvy * ty;
    float lt = -vt * c->tangent_mass;
    float maxf = c->friction * (lambda > 0.0f ? lambda : 0.0f);
    if (lt > maxf) lt = maxf;
    if (lt < -maxf) lt = -maxf;

    ix = lt * tx; iy = lt * ty;
    a->vel[0] -= ix * a->inv_mass;
    a->vel[1] -= iy * a->inv_mass;
    a->angvel -= (c->ra[0] * iy - c->ra[1] * ix) * a->inv_inertia;
    b->vel[0] += ix * b->inv_mass;
    b->vel[1] += iy * b->inv_mass;
    b->angvel += (c->rb[0] * iy - c->rb[1] * ix) * b->inv_inertia;
}

/* --------------------------------------------------------- the world step */

void sdph2_step(sdph2_world *w, float dt) {
    if (!w || dt <= 0.0f) return;
    if (w->iterations < 1) w->iterations = 1;

    /* 1: the velocities */
    float damp = 1.0f / (1.0f + w->damping * dt * 60.0f);
    float adamp = 1.0f / (1.0f + w->ang_damping * dt * 60.0f);
    for (int i = 0; i < w->count; ++i) {
        sdph2_body *b = &w->bodies[i];
        if (b->inv_mass <= 0.0f) continue;
        if (b->sleeping) continue;
        b->vel[0] += w->gravity[0] * dt;
        b->vel[1] += w->gravity[1] * dt;
        b->vel[0] *= damp;
        b->vel[1] *= damp;
        b->angvel *= adamp;
    }

    /* 2: the positions */
    for (int i = 0; i < w->count; ++i) {
        sdph2_body *b = &w->bodies[i];
        if (b->inv_mass <= 0.0f || b->sleeping) continue;
        b->pos[0] += b->vel[0] * dt;
        b->pos[1] += b->vel[1] * dt;
        b->angle += b->angvel * dt;
    }

    /* 3: the contacts (sweep and prune on the X axis) */
    ph2_cache *cache = cache_get(w);
    int n = 0;
    contact *cs = NULL;
    if (cache && cache_reserve(cache, w->count) == 0 &&
        cache_reserve_contacts(cache, MAX_CONTACTS) == 0) {
        cs = cache->contacts;
        /* the bounds and the sorted order are computed once per step */
        for (int i = 0; i < w->count; ++i) {
            body_aabb(&w->bodies[i], cache->aabb + (size_t)i * 4u);
            cache->order[i] = i;
        }
        cache_sort(cache);

        for (int si = 0; si < cache->count && n < MAX_CONTACTS; ++si) {
            int i = cache->order[si];
            sdph2_body *a = &w->bodies[i];
            const float *ba = cache->aabb + (size_t)i * 4u;
            int a_active = (a->inv_mass > 0.0f) && !a->sleeping;
            for (int sj = si + 1; sj < cache->count && n < MAX_CONTACTS; ++sj) {
                int k = cache->order[sj];
                const float *bb = cache->aabb + (size_t)k * 4u;
                if (bb[0] > ba[2]) break;          /* the sweep ends here */
                sdph2_body *b = &w->bodies[k];
                int b_active = (b->inv_mass > 0.0f) && !b->sleeping;
                if (!a_active && !b_active) continue;
                if (ba[3] < bb[1] || bb[3] < ba[1]) continue;  /* the Y overlap */
                contact c;
                if (collide(a, b, &c)) {
                    prepare_contact(&c, dt);
                    cs[n++] = c;
                }
            }
        }
    }
    w->contacts = n;

    /* 4: the solver */
    for (int it = 0; it < w->iterations; ++it)
        for (int i = 0; i < n; ++i)
            solve_contact(&cs[i]);

    /* 5: the positional correction (so the bodies do not sink) */
    for (int i = 0; i < n; ++i) {
        contact *c = &cs[i];
        float total = c->a->inv_mass + c->b->inv_mass;
        if (total <= 0.0f) continue;
        float corr = (c->depth - 0.01f) * 0.4f / total;
        if (corr <= 0.0f) continue;
        c->a->pos[0] -= c->nx * corr * c->a->inv_mass;
        c->a->pos[1] -= c->ny * corr * c->a->inv_mass;
        c->b->pos[0] += c->nx * corr * c->b->inv_mass;
        c->b->pos[1] += c->ny * corr * c->b->inv_mass;
    }

    /* 6: the world bounds */
    if (w->use_bounds) {
        for (int i = 0; i < w->count; ++i) {
            sdph2_body *b = &w->bodies[i];
            if (b->inv_mass <= 0.0f) continue;
            float r = (b->shape == SDPH2_CIRCLE) ? b->radius : b->bound;
            if (b->pos[0] - r < w->bounds[0]) {
                b->pos[0] = w->bounds[0] + r;
                if (b->vel[0] < 0.0f) b->vel[0] = -b->vel[0] * b->restitution;
                b->sleeping = 0;
            }
            if (b->pos[0] + r > w->bounds[2]) {
                b->pos[0] = w->bounds[2] - r;
                if (b->vel[0] > 0.0f) b->vel[0] = -b->vel[0] * b->restitution;
                b->sleeping = 0;
            }
            if (b->pos[1] - r < w->bounds[1]) {
                b->pos[1] = w->bounds[1] + r;
                if (b->vel[1] < 0.0f) b->vel[1] = -b->vel[1] * b->restitution;
                b->sleeping = 0;
            }
            if (b->pos[1] + r > w->bounds[3]) {
                b->pos[1] = w->bounds[3] - r;
                if (b->vel[1] > 0.0f) b->vel[1] = -b->vel[1] * b->restitution;
                b->sleeping = 0;
            }
        }
    }

    /* 7: the sleeping (a body must be slow for a while) */
    for (int i = 0; i < w->count; ++i) {
        sdph2_body *b = &w->bodies[i];
        if (b->inv_mass <= 0.0f) continue;
        float v2 = b->vel[0] * b->vel[0] + b->vel[1] * b->vel[1];
        if (v2 < 0.25f && fabsf(b->angvel) < 0.6f) {
            b->sleep_timer += dt;
            if (b->sleep_timer > 0.4f && !b->sleeping) {
                b->sleeping = 1;
                b->vel[0] = b->vel[1] = 0.0f;
                b->angvel = 0.0f;
            }
        } else {
            b->sleep_timer = 0.0f;
            if (b->sleeping) b->sleeping = 0;
        }
    }

    /* the contact buffer belongs to the cache */
}
