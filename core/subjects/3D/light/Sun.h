/* Sun.h - Silver Diamond Game Engine :: directional light subject */
#ifndef SDGE_SUN_H
#define SDGE_SUN_H

#include "../../../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sdge_sun {
    float dir[3];        /* direction the light TRAVELS (normalized on apply) */
    float color[3];
    float intensity;
} sdge_sun;

SDGE_API void sdge_sun_init(sdge_sun *s, const float dir[3],
                            const float color[3], float intensity);
/* push into the renderer (call once per frame, before drawing) */
SDGE_API void sdge_sun_apply(const sdge_sun *s);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_SUN_H */
