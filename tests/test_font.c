/* test_font.c - automated tests for the font loader (TTF, OTF and the
 * built-in bitmap font).
 *
 * Exit code = number of failed checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "assets/font.h"

static int g_failed = 0, g_total = 0;
#define CHECK(cond, name) do { \
    g_total++; \
    if (cond) printf("  [PASS] %s\n", (name)); \
    else { g_failed++; printf("  [FAIL] %s  (%s:%d)\n", (name), __FILE__, __LINE__); } \
} while (0)

/* counts the ink pixels of a glyph in the atlas */
static int glyph_ink(const sdga_font *f, int gid) {
    const sdga_glyph *g = sdga_font_glyph(f, gid);
    if (!f || !g || g->w <= 0) return 0;
    const unsigned char *px = sdga_font_pixels(f);
    int w = 0, h = 0;
    sdga_font_texture_size(f, &w, &h);
    if (!px) return 0;
    int gx = (int)(g->u0 * (float)w);
    int gy = (int)(g->v0 * (float)h);
    int ink = 0;
    for (int y = 0; y < g->h && gy + y < h; ++y)
        for (int x = 0; x < g->w && gx + x < w; ++x)
            if (px[(size_t)(gy + y) * (size_t)w + (size_t)(gx + x)]) ink++;
    return ink;
}

static void test_builtin(void) {
    printf("\n  -- the built-in font --\n");
    sdga_font *f = sdga_load_builtin_font(14.0f);
    CHECK(f != NULL, "builtin: it loads");
    if (!f) return;
    CHECK(sdga_font_glyph_count(f) == 95, "builtin: 95 glyphs");
    int gid = sdga_font_glyph_index(f, 'A');
    CHECK(gid == 'A' - 32, "builtin: the glyph index");
    const sdga_glyph *g = sdga_font_glyph(f, gid);
    CHECK(g != NULL && g->w == 5 && g->h == 7, "builtin: the glyph is 5x7");
    CHECK(g != NULL && fabsf(g->advance - 6.0f) < 0.001f,
          "builtin: the advance is 6 pixels");
    CHECK(glyph_ink(f, gid) > 5, "builtin: the glyph has ink");
    CHECK(sdga_font_ascent(f) > 0.0f, "builtin: the ascent is positive");
    sdga_free_font(f);
}

static void test_file(const char *path, const char *tag) {
    printf("\n  -- %s --\n", tag);
    sdga_font *f = sdga_load_font(path, 32.0f);
    CHECK(f != NULL, "the font loads");
    if (!f) return;

    CHECK(sdga_font_glyph_count(f) > 100, "the font has many glyphs");
    CHECK(sdga_font_ascent(f) > 5.0f, "the ascent is sane");
    CHECK(sdga_font_descent(f) > 0.0f, "the descent is sane");
    CHECK(sdga_font_line_height(f) > sdga_font_ascent(f), "the line height");

    int w = 0, h = 0;
    CHECK(sdga_font_texture_size(f, &w, &h) == 0 && w > 0 && h > 0,
          "the atlas has a size");

    /* the letters */
    int gid_a = sdga_font_glyph_index(f, 'A');
    CHECK(gid_a > 0, "'A' has a glyph");
    const sdga_glyph *ga = sdga_font_glyph(f, gid_a);
    CHECK(ga != NULL && ga->w > 4 && ga->h > 6, "'A' has a bitmap");
    CHECK(ga != NULL && ga->advance > 4.0f, "'A' has an advance");
    CHECK(ga != NULL && ga->u1 > ga->u0 && ga->v1 > ga->v0, "'A' has uv");
    CHECK(glyph_ink(f, gid_a) > 20, "'A' has ink");

    int gid_g = sdga_font_glyph_index(f, 'g');
    const sdga_glyph *gg = sdga_font_glyph(f, gid_g);
    CHECK(gg != NULL && gg->bearing_y < ga->bearing_y,
          "'g' sits lower than 'A' (the descender)");

    /* the space has no bitmap but an advance */
    int gid_s = sdga_font_glyph_index(f, ' ');
    const sdga_glyph *gs = sdga_font_glyph(f, gid_s);
    CHECK(gs != NULL && gs->advance > 0.0f, "the space has an advance");

    /* the digits */
    int gid_1 = sdga_font_glyph_index(f, '1');
    CHECK(gid_1 != gid_a, "the digits have their own glyphs");
    CHECK(glyph_ink(f, gid_1) > 5, "'1' has ink");

    /* the name */
    CHECK(strstr(sdga_font_name(f), "font") != NULL ||
          strlen(sdga_font_name(f)) > 0, "the font has a name");

    /* the size faces: the text is never scaled down */
    CHECK(sdga_font_face_count(f) >= 1, "the font has at least one face");
    int face = sdga_font_face_for(f, 16.0f);
    CHECK(face >= 0, "a face is found for the size 16");
    float native = sdga_font_face_size(f, face);
    CHECK(native > 0.0f, "the face has a native size");
    const sdga_glyph *gf = sdga_font_face_glyph(f, face, 'A');
    CHECK(gf != NULL && gf->w > 4, "the face has the glyph of 'A'");
    /* the faces must not overlap in the atlas */
    int faces = sdga_font_face_count(f);
    for (int i = 0; i < faces; ++i) {
        for (int k = i + 1; k < faces; ++k) {
            const sdga_glyph *a = sdga_font_face_glyph(f, i, 'A');
            const sdga_glyph *b = sdga_font_face_glyph(f, k, 'A');
            if (!a || !b) continue;
            int overlap = (a->u0 < b->u1 && b->u0 < a->u1
                           && a->v0 < b->v1 && b->v0 < a->v1);
            CHECK(!overlap, "the faces do not overlap in the atlas");
        }
    }

    sdga_free_font(f);
}

int main(void) {
    test_builtin();
    test_file("tests/data/Roboto-Regular.ttf", "TrueType (glyf/loca)");
    test_file("tests/data/SourceSerif4-Regular.otf", "OpenType (CFF)");

    printf("\n  -- the error paths --\n");
    CHECK(sdga_load_font("tests/data/no_such_font.ttf", 16.0f) == NULL,
          "a missing file fails");
    CHECK(sdga_load_font(NULL, 16.0f) == NULL, "a NULL path fails");
    CHECK(sdga_load_font("tests/data/Roboto-Regular.ttf", 0.0f) == NULL,
          "a zero size fails");

    /* the faces of a loaded font */
    {
        sdga_font *f = sdga_load_font("tests/data/Roboto-Regular.ttf", 30.0f);
        CHECK(f != NULL, "the font loads for the faces");
        if (f) {
            CHECK(sdga_font_add_face(f, 14.0f) == 0, "a face is added");
            CHECK(sdga_font_add_face(f, 14.0f) == 0, "the same size is reused");
            CHECK(sdga_font_face_count(f) == 2, "the face count");
            int f14 = sdga_font_face_for(f, 14.0f);
            CHECK(sdga_font_face_size(f, f14) == 14.0f, "the face size");
            const sdga_glyph *a = sdga_font_face_glyph(f, 0, 'A');
            const sdga_glyph *b = sdga_font_face_glyph(f, f14, 'A');
            CHECK(a && b && a->h > b->h, "the 14 px face is smaller");
            sdga_free_font(f);
        }
    }

    printf("\ntest_font: %d checks, %d failed\n", g_total, g_failed);
    return g_failed;
}
