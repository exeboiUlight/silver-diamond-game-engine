/* Camera.c - Silver Diamond Game Engine :: 3D camera implementation */
#include "Camera.h"

#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

SDGE_API void sdge_camera3d_init(sdge_camera3d *c, float fov_deg,
                                 float aspect, float znear, float zfar) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->up[1] = 1.0f;
    c->fov_deg = fov_deg > 1.0f ? fov_deg : 60.0f;
    c->aspect = aspect > 0.0f ? aspect : 1.0f;
    c->znear = znear > 0.001f ? znear : 0.1f;
    c->zfar = zfar > c->znear ? zfar : 100.0f;
    c->dist = 5.0f;
    m4_identity(c->view);
    m4_identity(c->proj);
    sdge_camera3d_orbit(c, c->up, 5.0f, 0.0f, 0.4f);
}

SDGE_API void sdge_camera3d_orbit(sdge_camera3d *c, const float center[3],
                                  float dist, float yaw, float pitch) {
    if (!c) return;
    if (center) memcpy(c->center, center, sizeof(float) * 3);
    c->dist = dist > 0.05f ? dist : 0.05f;
    c->yaw = yaw;
    c->pitch = pitch < -1.5f ? -1.5f : (pitch > 1.5f ? 1.5f : pitch);

    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    float cy = cosf(c->yaw),   sy = sinf(c->yaw);
    c->pos[0] = c->center[0] + c->dist * cp * sy;
    c->pos[1] = c->center[1] + c->dist * sp;
    c->pos[2] = c->center[2] + c->dist * cp * cy;
    memcpy(c->target, c->center, sizeof(float) * 3);
}

SDGE_API void sdge_camera3d_fps(sdge_camera3d *c, const float pos[3],
                                float yaw, float pitch) {
    if (!c) return;
    if (pos) memcpy(c->pos, pos, sizeof(float) * 3);
    c->yaw = yaw;
    c->pitch = pitch < -1.5f ? -1.5f : (pitch > 1.5f ? 1.5f : pitch);

    float cp = cosf(c->pitch), sp = sinf(c->pitch);
    float cy = cosf(c->yaw),   sy = sinf(c->yaw);
    c->target[0] = c->pos[0] + cp * sy;
    c->target[1] = c->pos[1] + sp;
    c->target[2] = c->pos[2] + cp * cy;
}

SDGE_API void sdge_camera3d_look_at(sdge_camera3d *c, const float eye[3],
                                     const float at[3]) {
    if (!c) return;
    if (eye) memcpy(c->pos, eye, sizeof(float) * 3);
    if (at)  memcpy(c->target, at, sizeof(float) * 3);
}

SDGE_API void sdge_camera3d_set_aspect(sdge_camera3d *c, float aspect) {
    if (c && aspect > 0.0f) c->aspect = aspect;
}

SDGE_API void sdge_camera3d_update(sdge_camera3d *c) {
    if (!c) return;
    m4_look_at(c->view, c->pos, c->target, c->up);
    m4_perspective(c->proj, (float)(c->fov_deg * M_PI / 180.0),
                   c->aspect, c->znear, c->zfar);
    m4_mul(c->vp, c->proj, c->view);
    frustum_from_vp(c->frustum, c->vp);
}

SDGE_API void sdge_camera3d_forward(const sdge_camera3d *c, float out[3]) {
    if (!c || !out) return;
    sdg_vec3 f;
    v3_sub(f, c->target, c->pos);
    v3_norm(out, f);
}

SDGE_API int sdge_camera3d_sphere_visible(const sdge_camera3d *c,
                                           const float center[3], float radius) {
    if (!c || !center) return 1;
    return sphere_in_frustum(c->frustum, center, radius);
}
