/* Camera.h - Silver Diamond Game Engine :: 3D camera subject
 *
 * Two control styles:
 *   - orbit: camera circles a center point (yaw/pitch/dist) - good for
 *     model viewers and cutscenes;
 *   - free/fps: position + yaw/pitch, forward is derived.
 *
 * The camera keeps view/proj matrices and the frustum planes in sync
 * (sdge_camera3d_update) so culling is one cheap sphere test.
 */
#ifndef SDGE_CAMERA3D_H
#define SDGE_CAMERA3D_H

#include "../../sdge_def.h"
#include "../../math/math3d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_camera3d {
    float pos[3];
    float target[3];          /* look-at point */
    float up[3];              /* world up hint, usually (0,1,0) */
    float fov_deg;            /* vertical field of view */
    float aspect;             /* width / height */
    float znear, zfar;

    /* orbit state */
    float center[3];
    float dist, yaw, pitch;

    /* derived (valid after sdge_camera3d_update) */
    sdg_mat4 view, proj, vp;
    float   frustum[6][4];    /* inward planes, for sphere culling */
} sdge_camera3d;

/* fov in degrees, aspect = w/h */
SDGE_API void sdge_camera3d_init(sdge_camera3d *c, float fov_deg,
                                 float aspect, float znear, float zfar);

/* orbit mode: yaw/pitch in radians, dist > 0 */
SDGE_API void sdge_camera3d_orbit(sdge_camera3d *c, const float center[3],
                                  float dist, float yaw, float pitch);

/* free mode: derive target from position + yaw/pitch (forward = -Z at 0,0) */
SDGE_API void sdge_camera3d_fps(sdge_camera3d *c, const float pos[3],
                                 float yaw, float pitch);

/* direct look-at (either mode may also just set pos/target manually) */
SDGE_API void sdge_camera3d_look_at(sdge_camera3d *c, const float eye[3],
                                     const float at[3]);
SDGE_API void sdge_camera3d_set_aspect(sdge_camera3d *c, float aspect);

/* recompute view/proj/vp/frustum; call after changing any field above */
SDGE_API void sdge_camera3d_update(sdge_camera3d *c);

/* forward vector (normalized), after update */
SDGE_API void sdge_camera3d_forward(const sdge_camera3d *c, float out[3]);

/* frustum sphere test (after update) */
SDGE_API int sdge_camera3d_sphere_visible(const sdge_camera3d *c,
                                           const float center[3], float radius);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_CAMERA3D_H */
