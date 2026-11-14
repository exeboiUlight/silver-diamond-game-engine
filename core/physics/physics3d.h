/* physics3d.h - Silver Diamond Game Engine :: 3D rigid body physics
 *
 * The 3D counterpart of physics2d.h:
 *   - shapes   : spheres and oriented boxes (OBB)
 *   - narrow   : SAT for the boxes (15 axes), the exact sphere tests
 *   - solver   : sequential impulses with restitution and friction
 *   - bodies   : sleep when they stop moving, wake on a collision
 *
 * The orientation is stored as a quaternion (the real state) and the euler
 * angles are only a readable view of it - integrating the euler angles
 * directly makes a body "twist" around the wrong axis as soon as two axes
 * rotate at the same time.
 *
 * The inertia is a full diagonal tensor (one value per local axis), so a
 * long box rotates differently around X and around Z - a single scalar
 * inertia makes boxes spin like a top.
 *
 * The body pointers are STABLE: adding or removing a body never moves the
 * others, so a pointer returned by sdph3_add_box() stays valid.
 */
#ifndef SDGE_PHYSICS3D_H
#define SDGE_PHYSICS3D_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SDPH3_SPHERE = 0,
    SDPH3_BOX    = 1
} sdph3_shape;

/* the body types */
#define SDPH3_STATIC     0   /* immovable (the world itself)        */
#define SDPH3_DYNAMIC    1   /* moved by the solver                */
#define SDPH3_KINEMATIC  2   /* moved by the game, pushes the bodies */

typedef struct {
    int    shape;             /* SDPH3_SPHERE / SDPH3_BOX */
    int    type;              /* SDPH3_STATIC / DYNAMIC / KINEMATIC */
    float  pos[3];
    float  vel[3];
    float  angle[3];          /* the euler view (radians, Rz*Ry*Rx) */
    float  angvel[3];         /* the world space angular velocity */
    float  quat[4];           /* the orientation (x, y, z, w) */
    float  radius;            /* the sphere radius */
    float  half[3];           /* the box half extents */
    float  inv_mass;
    float  inv_inertia;       /* the mean inverse inertia (compat) */
    float  inv_inertia_axis[3]; /* the diagonal inverse inertia tensor */
    float  restitution;
    float  friction;
    float  linear_damping;    /* 0 = the world damping is used */
    float  angular_damping;
    float  bound;             /* the bounding radius */
    float  sleep_timer;       /* how long the body has been slow */
    int    sleeping;
    int    tag;
} sdph3_body;

typedef struct {
    sdph3_body **bodies;      /* stable pointers - never reallocated */
    int   count, cap;
    float gravity[3];
    float damping;
    float ang_damping;
    int   iterations;
    int   contacts;
    int   use_bounds;
    float bounds[6];          /* minx, miny, minz, maxx, maxy, maxz */
    void *cache;              /* the broadphase cache (internal) */
} sdph3_world;

SDGE_API sdph3_world *sdph3_world_create(void);
SDGE_API void sdph3_world_destroy(sdph3_world *w);

SDGE_API void sdph3_world_set_gravity(sdph3_world *w, float gx, float gy,
                                       float gz);
SDGE_API void sdph3_world_set_bounds(sdph3_world *w, int use,
                                      float minx, float miny, float minz,
                                      float maxx, float maxy, float maxz);
SDGE_API void sdph3_world_set_damping(sdph3_world *w, float linear,
                                       float angular);
SDGE_API void sdph3_world_set_iterations(sdph3_world *w, int iterations);

SDGE_API sdph3_body *sdph3_add_sphere(sdph3_world *w, float x, float y,
                                      float z, float radius, float mass);
SDGE_API sdph3_body *sdph3_add_box(sdph3_world *w, float x, float y, float z,
                                   float hw, float hh, float hd,
                                   float mass);
SDGE_API void sdph3_remove(sdph3_world *w, const sdph3_body *b);

SDGE_API void sdph3_step(sdph3_world *w, float dt);

/* the body state */
SDGE_API void sdph3_set_velocity(sdph3_body *b, float vx, float vy, float vz);
SDGE_API void sdph3_set_angular_velocity(sdph3_body *b, float wx, float wy,
                                         float wz);
SDGE_API void sdph3_set_position(sdph3_body *b, float x, float y, float z);
SDGE_API void sdph3_set_angle(sdph3_body *b, float ax, float ay, float az);
SDGE_API void sdph3_set_quat(sdph3_body *b, const float q[4]);
SDGE_API void sdph3_get_quat(const sdph3_body *b, float out[4]);
SDGE_API void sdph3_set_type(sdph3_body *b, int type);
SDGE_API int  sdph3_get_type(const sdph3_body *b);
SDGE_API void sdph3_set_restitution(sdph3_body *b, float restitution);
SDGE_API void sdph3_set_friction(sdph3_body *b, float friction);
SDGE_API void sdph3_wake(sdph3_body *b);
SDGE_API void sdph3_sleep(sdph3_body *b);

SDGE_API void sdph3_apply_impulse(sdph3_body *b, float ix, float iy, float iz,
                                  float rx, float ry, float rz);
SDGE_API int sdph3_point_in_body(const sdph3_body *b, float x, float y,
                                 float z);
SDGE_API int sdph3_query_sphere(sdph3_world *w, float x, float y, float z,
                                float r, sdph3_body **out, int max);
SDGE_API float sdph3_energy(const sdph3_world *w);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_PHYSICS3D_H */
