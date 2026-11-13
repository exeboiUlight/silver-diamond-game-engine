/* texture.h - Silver Diamond Game Engine :: CPU image decoding (stb_image)
 *
 * Decodes a PNG/JPG/TGA/BMP/HDR file into an RGBA8 buffer. This is the
 * CPU half only - uploading to a GL texture and sampling is the renderer's
 * job (core/graphics/renderer.h, sdgr_upload_texture).
 *
 *   sdga_image img;
 *   if (sdga_image_load_rgba("wood.png", &img) == 0) {
 *       // img.pixels - RGBA8, img.w * img.h * 4 bytes, top-left row first
 *       sdga_image_free(&img);
 *   }
 */
#ifndef SDGE_ASSETS_TEXTURE_H
#define SDGE_ASSETS_TEXTURE_H

#include "../sdge_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int            w, h;
    int            channels;   /* the decoded channel count (1/2/3/4) */
    unsigned char *pixels;     /* RGBA8, malloc'd, top row first */
} sdga_image;

/* decodes any stb_image format; 0 on success.
 * `want_alpha` = 1 asks for RGBA (RGB inputs get alpha=255);
 * 0 keeps RGB for opaque files (smaller uploads). */
SDGE_API int  sdga_image_load(const char *path, sdga_image *out);
SDGE_API int  sdga_image_load_rgba(const char *path, sdga_image *out);
/* decodes an image that is already in memory (GLB bufferView, data-URI) */
SDGE_API int  sdga_image_load_rgba_mem(const void *data, int size,
                                       sdga_image *out);
SDGE_API void sdga_image_free(sdga_image *img);

#ifdef __cplusplus
}
#endif
#endif /* SDGE_ASSETS_TEXTURE_H */