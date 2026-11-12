/* PointLight.h - Silver Diamond Game Engine :: point light subject */
#ifndef SDGE_POINTLIGHT_H
#define SDGE_POINTLIGHT_H

#include "../../../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_point_light {
    float pos[3];
    float color[3];
    float intensity;
    float radius;        /* attenuation distance, > 0 */
} sdge_point_light;

SDGE_API void sdge_point_light_init(sdge_point_light *l, const float pos[3],
                                     const float color[3],
                                     float intensity, float radius);
/* index: renderer light slot 0..SDGR_MAX_POINT_LIGHTS-1 */
SDGE_API void sdge_point_light_apply(const sdge_point_light *l, int index);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_POINTLIGHT_H */
