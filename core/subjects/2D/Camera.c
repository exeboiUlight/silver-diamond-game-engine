/* Camera.c - Silver Diamond Game Engine :: 2D camera implementation */
#include "Camera.h"

#include <string.h>
#include <math.h>

/* view = translate(-pos) * rotate(-rot) * scale(1/zoom) */
static void cam2d_view(sdge_camera2d *c) {
    float sx = (c->zoom > 0.0001f) ? c->zoom : 1.0f;
    float cs = cosf(-c->rotation), sn = sinf(-c->rotation);

    memset(c->view, 0, sizeof(c->view));
    c->view[0]  = cs * sx;
    c->view[1]  = sn * sx;
    c->view[4]  = -sn * sx;
    c->view[5]  = cs * sx;
    c->view[12] = -c->pos[0];
    c->view[13] = -c->pos[1];
    c->view[15] = 1.0f;
}

SDGE_API void sdge_camera2d_init(sdge_camera2d *c, int width, int height) {
    if (!c) return;
    memset(c, 0, sizeof(*c));
    c->zoom = 1.0f;
    c->viewport_w = width > 0 ? width : 1;
    c->viewport_h = height > 0 ? height : 1;
    sdge_camera2d_update(c);
}

SDGE_API void sdge_camera2d_set_viewport(sdge_camera2d *c, int w, int h) {
    if (!c) return;
    if (w > 0) c->viewport_w = w;
    if (h > 0) c->viewport_h = h;
}

SDGE_API void sdge_camera2d_set_zoom(sdge_camera2d *c, float zoom) {
    if (c && zoom > 0.0001f) c->zoom = zoom;
}

SDGE_API void sdge_camera2d_set_rotation(sdge_camera2d *c, float radians) {
    if (c) c->rotation = radians;
}

SDGE_API void sdge_camera2d_set_pos(sdge_camera2d *c, float x, float y) {
    if (!c) return;
    c->pos[0] = x;
    c->pos[1] = y;
}

SDGE_API void sdge_camera2d_update(sdge_camera2d *c) {
    if (!c) return;
    float w = (float)(c->viewport_w > 0 ? c->viewport_w : 1);
    float h = (float)(c->viewport_h > 0 ? c->viewport_h : 1);

    /* ortho with the origin in the centre, y up */
    m4_ortho(c->proj, -w * 0.5f, w * 0.5f, -h * 0.5f, h * 0.5f, -1.0f, 1.0f);
    cam2d_view(c);
    m4_mul(c->vp, c->proj, c->view);
}

SDGE_API void sdge_camera2d_screen_to_world(const sdge_camera2d *c,
                                            float sx, float sy, float out[2]) {
    if (!out) return;
    if (!c) { out[0] = sx; out[1] = sy; return; }
    /* screen -> viewport centre -> world */
    float w = (float)(c->viewport_w > 0 ? c->viewport_w : 1);
    float h = (float)(c->viewport_h > 0 ? c->viewport_h : 1);
    float dx = sx - w * 0.5f;
    float dy = h * 0.5f - sy;            /* flip: screen y grows down */

    float cs = cosf(c->rotation), sn = sinf(c->rotation);
    float z = (c->zoom > 0.0001f) ? c->zoom : 1.0f;
    float rx = (dx * cs - dy * sn) * z;
    float ry = (dx * sn + dy * cs) * z;
    out[0] = rx + c->pos[0];
    out[1] = ry + c->pos[1];
}

SDGE_API void sdge_camera2d_world_to_screen(const sdge_camera2d *c,
                                            float wx, float wy, float out[2]) {
    if (!out) return;
    if (!c) { out[0] = wx; out[1] = wy; return; }
    float dx = wx - c->pos[0];
    float dy = wy - c->pos[1];

    float cs = cosf(-c->rotation), sn = sinf(-c->rotation);
    float z = (c->zoom > 0.0001f) ? c->zoom : 1.0f;
    float rx = (dx * cs - dy * sn) * z;
    float ry = (dx * sn + dy * cs) * z;

    float w = (float)(c->viewport_w > 0 ? c->viewport_w : 1);
    float h = (float)(c->viewport_h > 0 ? c->viewport_h : 1);
    out[0] = rx + w * 0.5f;
    out[1] = h * 0.5f - ry;
}

SDGE_API void sdge_camera2d_bounds(const sdge_camera2d *c, float out[4]) {
    if (!out) return;
    if (!c) { out[0] = out[1] = out[2] = out[3] = 0.0f; return; }
    float w = (float)(c->viewport_w > 0 ? c->viewport_w : 1);
    float h = (float)(c->viewport_h > 0 ? c->viewport_h : 1);
    float hw = w * 0.5f / (c->zoom > 0.0001f ? c->zoom : 1.0f);
    float hh = h * 0.5f / (c->zoom > 0.0001f ? c->zoom : 1.0f);
    out[0] = c->pos[0] - hw;
    out[1] = c->pos[1] - hh;
    out[2] = hw * 2.0f;
    out[3] = hh * 2.0f;
}
