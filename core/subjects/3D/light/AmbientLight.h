/* AmbientLight.h - Silver Diamond Game Engine :: ambient light subject */
#ifndef SDGE_AMBIENTLIGHT_H
#define SDGE_AMBIENTLIGHT_H

#include "../../../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_ambient_light {
    float color[3];     /* ambient irradiance color */
} sdge_ambient_light;

SDGE_API void sdge_ambient_light_init(sdge_ambient_light *a,
                                      const float color[3]);
SDGE_API void sdge_ambient_light_apply(const sdge_ambient_light *a);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_AMBIENTLIGHT_H */
