/* PointLight.c - Silver Diamond Game Engine :: point light implementation */
#include "PointLight.h"
#include "../../../graphics/renderer.h"

#include <string.h>

SDGE_API void sdge_point_light_init(sdge_point_light *l, const float pos[3],
                                    const float color[3],
                                    float intensity, float radius) {
    if (!l) return;
    l->pos[0] = l->pos[1] = l->pos[2] = 0.0f;
    l->color[0] = l->color[1] = l->color[2] = 1.0f;
    l->intensity = 1.0f;
    l->radius = 5.0f;
    if (pos)   memcpy(l->pos, pos, sizeof(float) * 3);
    if (color) memcpy(l->color, color, sizeof(float) * 3);
    if (intensity > 0.0f) l->intensity = intensity;
    if (radius > 0.0f)     l->radius = radius;
}

SDGE_API void sdge_point_light_apply(const sdge_point_light *l, int index) {
    if (!l) return;
    sdgr_set_point_light(index, l->pos, l->color, l->intensity, l->radius);
}
