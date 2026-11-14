/* physics2d.h - Silver Diamond Game Engine :: 2D rigid body physics
 *
 * A small but complete rigid body engine:
 *   - shapes   : circles and oriented boxes (OBB)
 *   - narrow   : SAT for the boxes, the exact circle tests
 *   - solver   : sequential impulses with restitution and friction
 *   - bodies   : sleep when they stop moving, wake on a collision
 *   - world    : gravity, damping, an optional bounding box
 *
 * The engine is headless and deterministic: the same inputs always give
 * the same result, so it can be tested without a window.
 *
 * The body pointers are STABLE: adding or removing a body never moves the
 * others, so a pointer returned by sdph2_add_box() stays valid.
 *
 * Typical frame:
 *     sdph2_world *w = sdph2_world_create();
 *     sdph2_world_set_gravity(w, 0.0f, -9.81f);
 *     sdph2_add_box(w, 0, 5, 0.5f, 0.5f, 1.0f);
 *     sdph2_step(w, 1.0f / 60.0f);
 */
#ifndef SDGE_PHYSICS2D_H
#define SDGE_PHYSICS2D_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDPH2_CIRCLE = 0,
    SDPH2_BOX    = 1
} sdph2_shape;

/* the body types */
#define SDPH2_STATIC     0   /* immovable (the world itself)        */
#define SDPH2_DYNAMIC    1   /* moved by the solver                */
#define SDPH2_KINEMATIC  2   /* moved by the game, pushes the bodies */

typedef struct {
    int    shape;
    int    type;             /* SDPH2_STATIC / DYNAMIC / KINEMATIC */
    float  pos[2];          /* the centre of mass */
    float  vel[2];
    float  angle;           /* the box rotation (radians) */
    float  angvel;
    float  radius;          /* the circle radius */
    float  half[2];         /* the box half extents */
    float  inv_mass;        /* 0 = a static body */
    float  inv_inertia;
    float  restitution;     /* 0..1 */
    float  friction;        /* 0..1 */
    float  linear_damping;  /* 0 = the world damping is used */
    float  angular_damping;
    float  bound;           /* the bounding radius (the broadphase) */
    float  sleep_timer;     /* how long the body has been slow */
    int    sleeping;
    int    tag;             /* free for the application */
} sdph2_body;

typedef struct {
    sdph2_body **bodies;     /* stable pointers - never reallocated */
    int   count, cap;
    float gravity[2];
    float damping;          /* the linear damping (per second) */
    float ang_damping;
    int   iterations;       /* the solver iterations per step */
    int   contacts;         /* the contacts of the last step */
    int   use_bounds;
    float bounds[4];        /* minx, miny, maxx, maxy */
    void *cache;            /* the broadphase cache (internal) */
} sdph2_world;

SDGE_API sdph2_world *sdph2_world_create(void);
SDGE_API void sdph2_world_destroy(sdph2_world *w);

SDGE_API void sdph2_world_set_gravity(sdph2_world *w, float gx, float gy);
SDGE_API void sdph2_world_set_bounds(sdph2_world *w, int use,
                                     float minx, float miny,
                                     float maxx, float maxy);
SDGE_API void sdph2_world_set_damping(sdph2_world *w, float linear,
                                      float angular);
SDGE_API void sdph2_world_set_iterations(sdph2_world *w, int iterations);

/* adds a body; `mass` <= 0 makes it static */
SDGE_API sdph2_body *sdph2_add_circle(sdph2_world *w, float x, float y,
                                      float radius, float mass);
SDGE_API sdph2_body *sdph2_add_box(sdph2_world *w, float x, float y,
                                   float hw, float hh, float mass,
                                   float angle);
SDGE_API void sdph2_remove(sdph2_world *w, const sdph2_body *b);

/* runs the simulation */
SDGE_API void sdph2_step(sdph2_world *w, float dt);

/* helpers */
SDGE_API void sdph2_set_velocity(sdph2_body *b, float vx, float vy);
SDGE_API void sdph2_set_angular_velocity(sdph2_body *b, float w);
SDGE_API void sdph2_set_position(sdph2_body *b, float x, float y);
SDGE_API void sdph2_set_angle(sdph2_body *b, float angle);
SDGE_API void sdph2_set_type(sdph2_body *b, int type);
SDGE_API int  sdph2_get_type(const sdph2_body *b);
SDGE_API void sdph2_set_restitution(sdph2_body *b, float restitution);
SDGE_API void sdph2_set_friction(sdph2_body *b, float friction);
SDGE_API void sdph2_wake(sdph2_body *b);
SDGE_API void sdph2_sleep(sdph2_body *b);
SDGE_API void sdph2_apply_impulse(sdph2_body *b, float ix, float iy,
                                  float rx, float ry);
/* the bodies overlapping the circle (x, y, r) */
SDGE_API int sdph2_query_circle(sdph2_world *w, float x, float y, float r,
                                sdph2_body **out, int max);
/* true when the point is inside the body */
SDGE_API int sdph2_point_in_body(const sdph2_body *b, float x, float y);
/* the total kinetic energy (a sleeping test) */
SDGE_API float sdph2_energy(const sdph2_world *w);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_PHYSICS2D_H */
