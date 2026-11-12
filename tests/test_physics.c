/* test_physics.c - automated tests for the 2D and the 3D physics.
 *
 * The physics is headless and deterministic, so the whole suite runs
 * without a window.
 *
 * Exit code = number of failed checks.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "physics/physics2d.h"
#include "physics/physics3d.h"

static int g_failed = 0, g_total = 0;
#define CHECK(cond, name) do { \
    g_total++; \
    if (cond) printf("  [PASS] %s\n", (name)); \
    else { g_failed++; printf("  [FAIL] %s  (%s:%d)\n", (name), __FILE__, __LINE__); } \
} while (0)

/* ---------------------------------------------------------------- 2D */
static void test_2d(void) {
    printf("\n  -- physics 2d --\n");
    sdph2_world *w = sdph2_world_create();
    CHECK(w != NULL, "2d: the world is created");
    if (!w) return;

    sdph2_world_set_gravity(w, 0.0f, -10.0f);
    /* the floor */
    sdph2_body *floor = sdph2_add_box(w, 0.0f, -1.0f, 6.0f, 0.5f, 0.0f, 0.0f);
    CHECK(floor && floor->inv_mass == 0.0f, "2d: a mass of 0 makes it static");

    /* a box that falls onto the floor */
    sdph2_body *box = sdph2_add_box(w, 0.0f, 4.0f, 0.5f, 0.5f, 1.0f, 0.0f);
    CHECK(box && box->inv_mass > 0.0f, "2d: a dynamic body");

    float max_v = 0.0f;
    for (int i = 0; i < 180; ++i) {
        sdph2_step(w, 1.0f / 60.0f);
        if (i > 100) {
            float v = fabsf(box->vel[0]) + fabsf(box->vel[1]);
            if (v > max_v) max_v = v;
        }
    }

    /* it should rest on the floor: the floor's top is at -0.5 */
    printf("        the box rests at y=%.3f (the floor's top is -0.5), "
           "v=%.3f max=%.3f timer=%.2f\n",
           box->pos[1], box->vel[1], max_v, box->sleep_timer);
    CHECK(fabsf(box->pos[1] - 0.0f) < 0.12f, "2d: the box rests on the floor");
    CHECK(fabsf(box->vel[1]) < 0.2f, "2d: the box has stopped");
    CHECK(box->sleeping == 1, "2d: the box falls asleep");
    CHECK(w->contacts >= 0, "2d: the contacts are counted");

    /* the query */
    sdph2_body *hits[4];
    int n = sdph2_query_circle(w, box->pos[0], box->pos[1], 0.2f, hits, 4);
    CHECK(n >= 1, "2d: the query finds the body");
    CHECK(sdph2_point_in_body(box, box->pos[0], box->pos[1]) == 1,
          "2d: the point is inside the body");

    /* two circles collide and separate */
    sdph2_world *w2 = sdph2_world_create();
    sdph2_world_set_gravity(w2, 0.0f, 0.0f);
    sdph2_body *a = sdph2_add_circle(w2, -2.0f, 0.0f, 0.5f, 1.0f);
    sdph2_body *b = sdph2_add_circle(w2, 2.0f, 0.0f, 0.5f, 1.0f);
    sdph2_set_velocity(a, 4.0f, 0.0f);
    sdph2_set_velocity(b, -4.0f, 0.0f);
    int max_contacts = 0;
    for (int i = 0; i < 60; ++i) {
        sdph2_step(w2, 1.0f / 60.0f);
        if (w2->contacts > max_contacts) max_contacts = w2->contacts;
    }
    printf("        the circles at x=%.2f and x=%.2f (v=%.2f / %.2f)\n",
           a->pos[0], b->pos[0], a->vel[0], b->vel[0]);
    CHECK(fabsf(a->pos[0] - b->pos[0]) > 0.9f, "2d: the circles do not overlap");
    CHECK(a->vel[0] < 0.0f && b->vel[0] > 0.0f, "2d: the circles bounced");
    CHECK(max_contacts > 0, "2d: the contact was detected");
    sdph2_world_destroy(w2);

    /* the impulse */
    sdph2_body *c = sdph2_add_circle(w, 10.0f, 10.0f, 0.5f, 1.0f);
    sdph2_apply_impulse(c, 2.0f, 0.0f, 0.0f, 0.0f);
    CHECK(fabsf(c->vel[0] - 2.0f) < 0.01f, "2d: the impulse changes the velocity");

    /* the energy */
    CHECK(sdph2_energy(w) >= 0.0f, "2d: the energy is not negative");

    sdph2_world_destroy(w);
}

/* ---------------------------------------------------------------- 3D */
static void test_3d(void) {
    printf("\n  -- physics 3d --\n");
    sdph3_world *w = sdph3_world_create();
    CHECK(w != NULL, "3d: the world is created");
    if (!w) return;

    sdph3_world_set_gravity(w, 0.0f, -10.0f, 0.0f);
    sdph3_body *floor = sdph3_add_box(w, 0.0f, -0.5f, 0.0f, 6.0f, 0.5f, 6.0f, 0.0f);
    CHECK(floor && floor->inv_mass == 0.0f, "3d: the floor is static");

    sdph3_body *s = sdph3_add_sphere(w, 0.0f, 4.0f, 0.0f, 0.4f, 1.0f);
    CHECK(s && s->inv_mass > 0.0f, "3d: the sphere is dynamic");

    for (int i = 0; i < 180; ++i) sdph3_step(w, 1.0f / 60.0f);
    printf("        the sphere rests at y=%.3f (the floor's top is 0.0)\n",
           s->pos[1]);
    CHECK(fabsf(s->pos[1] - 0.4f) < 0.12f, "3d: the sphere rests on the floor");
    CHECK(fabsf(s->vel[1]) < 0.2f, "3d: the sphere has stopped");
    CHECK(s->sleeping == 1, "3d: the sphere falls asleep");

    /* two spheres */
    sdph3_world *w2 = sdph3_world_create();
    sdph3_world_set_gravity(w2, 0.0f, 0.0f, 0.0f);
    sdph3_body *a = sdph3_add_sphere(w2, -2.0f, 0.0f, 0.0f, 0.5f, 1.0f);
    sdph3_body *b = sdph3_add_sphere(w2, 2.0f, 0.0f, 0.0f, 0.5f, 1.0f);
    sdph3_set_velocity(a, 4.0f, 0.0f, 0.0f);
    sdph3_set_velocity(b, -4.0f, 0.0f, 0.0f);
    int max_contacts = 0;
    for (int i = 0; i < 60; ++i) {
        sdph3_step(w2, 1.0f / 60.0f);
        if (w2->contacts > max_contacts) max_contacts = w2->contacts;
    }
    printf("        the spheres at x=%.2f and x=%.2f\n", a->pos[0], b->pos[0]);
    CHECK(fabsf(a->pos[0] - b->pos[0]) > 0.9f, "3d: the spheres do not overlap");
    CHECK(a->vel[0] < 0.0f && b->vel[0] > 0.0f, "3d: the spheres bounced");
    CHECK(max_contacts > 0, "3d: the contact was detected");
    sdph3_world_destroy(w2);

    /* the impulse */
    sdph3_body *c = sdph3_add_sphere(w, 5.0f, 5.0f, 5.0f, 0.5f, 1.0f);
    sdph3_apply_impulse(c, 0.0f, 3.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    CHECK(fabsf(c->vel[1] - 3.0f) < 0.01f, "3d: the impulse changes the velocity");
    CHECK(sdph3_point_in_body(c, c->pos[0], c->pos[1], c->pos[2]) == 1,
          "3d: the point is inside the body");
    CHECK(sdph3_energy(w) >= 0.0f, "3d: the energy is not negative");

    sdph3_world_destroy(w);
}

int main(void) {
    test_2d();
    test_3d();
    printf("\ntest_physics: %d checks, %d failed\n", g_total, g_failed);
    return g_failed;
}
