/* AmbientLight.c - Silver Diamond Game Engine :: ambient light implementation */
#include "AmbientLight.h"
#include "../../../graphics/renderer.h"

#include <string.h>

SDGE_API void sdge_ambient_light_init(sdge_ambient_light *a,
                                       const float color[3]) {
    if (!a) return;
    a->color[0] = 0.2f; a->color[1] = 0.2f; a->color[2] = 0.25f;
    if (color) memcpy(a->color, color, sizeof(float) * 3);
}

SDGE_API void sdge_ambient_light_apply(const sdge_ambient_light *a) {
    if (!a) return;
    sdgr_set_ambient(a->color[0], a->color[1], a->color[2]);
}
