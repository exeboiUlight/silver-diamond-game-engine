/* Sun.c - Silver Diamond Game Engine :: directional light implementation */
#include "Sun.h"
#include "../../../graphics/renderer.h"

#include <string.h>

SDGE_API void sdge_sun_init(sdge_sun *s, const float dir[3],
                            const float color[3], float intensity) {
    if (!s) return;
    s->dir[0] = -0.5f; s->dir[1] = -1.0f; s->dir[2] = -0.3f;
    s->color[0] = s->color[1] = s->color[2] = 1.0f;
    s->intensity = 2.0f;
    if (dir)        memcpy(s->dir, dir, sizeof(float) * 3);
    if (color)      memcpy(s->color, color, sizeof(float) * 3);
    if (intensity > 0.0f) s->intensity = intensity;
}

SDGE_API void sdge_sun_apply(const sdge_sun *s) {
    if (!s) return;
    sdgr_set_sun(s->dir, s->color, s->intensity);
}
