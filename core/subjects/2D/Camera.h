/* Camera.h - Silver Diamond Game Engine :: 2D camera subject
 *
 * Orthographic camera for 2D scenes: position, zoom and rotation around
 * the viewport centre. The camera keeps a view and a projection matrix
 * (column major, like the 3D camera) so 2D and 3D subjects can share the
 * same renderer conventions.
 *
 * Screen space is pixels with the origin in the top-left corner, which is
 * what the UI renderer uses.
 */
#ifndef SDGE_CAMERA2D_H
#define SDGE_CAMERA2D_H

#include "../../sdge_def.h"
#include "../../math/math3d.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_camera2d {
    float pos[2];             /* world position of the viewport centre */
    float zoom;               /* > 0, 1 = 1:1 pixels */
    float rotation;           /* radians, around the viewport centre */
    int   viewport_w, viewport_h;

    /* derived (valid after sdge_camera2d_update) */
    sdg_mat4 view, proj, vp;
} sdge_camera2d;

SDGE_API void sdge_camera2d_init(sdge_camera2d *c, int width, int height);
SDGE_API void sdge_camera2d_set_viewport(sdge_camera2d *c, int w, int h);
SDGE_API void sdge_camera2d_set_zoom(sdge_camera2d *c, float zoom);
SDGE_API void sdge_camera2d_set_rotation(sdge_camera2d *c, float radians);
SDGE_API void sdge_camera2d_set_pos(sdge_camera2d *c, float x, float y);

/* recompute view/proj/vp; call after changing the camera */
SDGE_API void sdge_camera2d_update(sdge_camera2d *c);

/* screen (pixels, top-left origin) <-> world */
SDGE_API void sdge_camera2d_screen_to_world(const sdge_camera2d *c,
                                            float sx, float sy, float out[2]);
SDGE_API void sdge_camera2d_world_to_screen(const sdge_camera2d *c,
                                            float wx, float wy, float out[2]);

/* the visible world rectangle (x, y, w, h) */
SDGE_API void sdge_camera2d_bounds(const sdge_camera2d *c, float out[4]);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_CAMERA2D_H */
