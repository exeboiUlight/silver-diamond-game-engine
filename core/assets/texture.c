/* texture.c - Silver Diamond Game Engine :: CPU image decoding */
#include "texture.h"

#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_HDR
#include <stb_image.h>

static int img_load_impl(const char *path, sdga_image *out, int force_rgba) {
    if (!path || !out) return -1;
    memset(out, 0, sizeof(*out));

    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);   /* glTF: top-left origin */

    unsigned char *pixels = stbi_load(path, &w, &h, &ch,
                                      force_rgba ? 4 : 0);
    if (!pixels) return -2;
    if (w <= 0 || h <= 0) { stbi_image_free(pixels); return -3; }

    /* stb may hand back RGB for opaque images; widen to RGBA so the GL
     * upload is uniform (GL_RGBA8). */
    if (ch != 4) {
        size_t n = (size_t)w * (size_t)h;
        unsigned char *rgba = (unsigned char *)malloc(n * 4u);
        if (!rgba) { stbi_image_free(pixels); return -4; }
        for (size_t i = 0; i < n; ++i) {
            const unsigned char *s = pixels + i * (size_t)ch;
            unsigned char *d = rgba + i * 4u;
            d[0] = s[0];
            d[1] = ch >= 2 ? s[1] : s[0];
            d[2] = ch >= 3 ? s[2] : s[0];
            d[3] = ch >= 4 ? s[3] : 255;
        }
        stbi_image_free(pixels);
        pixels = rgba;
        ch = 4;
    }

    out->w = w;
    out->h = h;
    out->channels = ch;
    out->pixels = pixels;
    return 0;
}

SDGE_API int sdga_image_load(const char *path, sdga_image *out) {
    return img_load_impl(path, out, 0);
}

SDGE_API int sdga_image_load_rgba(const char *path, sdga_image *out) {
    return img_load_impl(path, out, 1);
}

SDGE_API void sdga_image_free(sdga_image *img) {
    if (!img) return;
    if (img->pixels) stbi_image_free(img->pixels);
    memset(img, 0, sizeof(*img));
}