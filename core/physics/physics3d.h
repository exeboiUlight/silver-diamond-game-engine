/* physics3d.h - Silver Diamond Game Engine :: 3D rigid body physics
 *
 * The 3D counterpart of physics2d.h:
 *   - shapes   : spheres and oriented boxes (OBB)
 *   - narrow   : SAT for the boxes (15 axes), the exact sphere tests
 *   - solver   : sequential impulses with restitution and friction
 *   - bodies   : sleep when they stop moving, wake on a collision
 *
 * The angular response uses a scalar inertia (an approximation of the
 * inertia tensor) - enough for stacks and bouncing balls.
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

typedef struct {
    int    shape;
    float  pos[3];
    float  vel[3];
    float  angle[3];       /* the box rotation (euler radians) */
    float  angvel[3];
    float  radius;         /* the sphere radius */
    float  half[3];        /* the box half extents */
    float  inv_mass;
    float  inv_inertia;
    float  restitution;
    float  friction;
    float  bound;          /* the bounding radius */
    float  sleep_timer;    /* how long the body has been slow */
    int    sleeping;
    int    tag;
} sdph3_body;

typedef struct {
    sdph3_body *bodies;
    int   count, cap;
    float gravity[3];
    float damping;
    float ang_damping;
    int   iterations;
    int   contacts;
    int   use_bounds;
    float bounds[6];       /* minx, miny, minz, maxx, maxy, maxz */
    void *cache;           /* the broadphase cache (internal) */
} sdph3_world;

SDGE_API sdph3_world *sdph3_world_create(void);
SDGE_API void sdph3_world_destroy(sdph3_world *w);

SDGE_API void sdph3_world_set_gravity(sdph3_world *w, float gx, float gy,
                                       float gz);
SDGE_API void sdph3_world_set_bounds(sdph3_world *w, int use,
                                      float minx, float miny, float minz,
                                      float maxx, float maxy, float maxz);

SDGE_API sdph3_body *sdph3_add_sphere(sdph3_world *w, float x, float y,
                                      float z, float radius, float mass);
SDGE_API sdph3_body *sdph3_add_box(sdph3_world *w, float x, float y, float z,
                                   float hw, float hh, float hd,
                                   float mass);
SDGE_API void sdph3_remove(sdph3_world *w, const sdph3_body *b);

SDGE_API void sdph3_step(sdph3_world *w, float dt);

SDGE_API void sdph3_set_velocity(sdph3_body *b, float vx, float vy, float vz);
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
