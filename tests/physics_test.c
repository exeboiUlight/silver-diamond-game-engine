/* tests/physics_test.c - Silver Diamond Game Engine :: the physics tests
 *
 * The physics does not need a window, so it can be tested on its own:
 *
 *     python build.py test
 *
 * The test runs a few simulations and checks the result:
 *   - a tilted box falls flat and comes to rest (no endless spinning);
 *   - a stack of boxes does not sink and does not drift;
 *   - a ball rolls: the linear and the angular speed are linked;
 *   - the pointers of the bodies stay valid when bodies are added/removed;
 *   - a sleeping body wakes up when something falls on it.
 */
#include <physics/physics2d.h>
#include <physics/physics3d.h>

#include <stdio.h>
#include <math.h>

static int failed;

static void check(const char *what, int ok) {
    printf("  %-46s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failed = 1;
}

/* --------------------------------------------------------------- the 3D */

static void test_3d(void) {
    printf("3D:\n");

    /* a tilted box must fall flat and stop */
    {
        sdph3_world *w = sdph3_world_create();
        sdph3_world_set_gravity(w, 0.0f, -9.81f, 0.0f);
        sdph3_add_box(w, 0, -1, 0, 20, 1, 20, 0.0f);
        sdph3_body *b = sdph3_add_box(w, 0, 3, 0, 0.5f, 0.5f, 0.5f, 1.0f);
        sdph3_set_angle(b, 0.8f, 0.0f, 0.0f);
        for (int i = 0; i < 900; ++i) sdph3_step(w, 1.0f / 60.0f);
        printf("    the box: y %.3f, angle %.3f, spin %.4f\n",
               b->pos[1], b->angle[0], b->angvel[0]);
        check("the tilted box came to rest", b->sleeping == 1);
        check("the tilted box stopped spinning",
              fabsf(b->angvel[0]) < 0.01f && fabsf(b->angvel[1]) < 0.01f &&
              fabsf(b->angvel[2]) < 0.01f);
        check("the tilted box lies on the floor",
              b->pos[1] > 0.4f && b->pos[1] < 0.6f);
        sdph3_world_destroy(w);
    }

    /* a stack must not sink or drift */
    {
        sdph3_world *w = sdph3_world_create();
        sdph3_world_set_gravity(w, 0.0f, -9.81f, 0.0f);
        sdph3_add_box(w, 0, -1, 0, 20, 1, 20, 0.0f);
        sdph3_body *s[4];
        for (int i = 0; i < 4; ++i)
            s[i] = sdph3_add_box(w, 0, 0.5f + 1.0f * i, 0, 0.5f, 0.5f, 0.5f,
                                 1.0f);
        for (int i = 0; i < 1200; ++i) sdph3_step(w, 1.0f / 60.0f);
        printf("    the stack: ");
        for (int i = 0; i < 4; ++i) printf("%.3f ", s[i]->pos[1]);
        printf("\n");
        check("the stack did not sink", s[0]->pos[1] > 0.42f);
        check("the stack did not drift",
              fabsf(s[3]->pos[0]) < 0.1f && fabsf(s[3]->pos[2]) < 0.1f);
        check("the stack came to rest", s[3]->sleeping == 1);
        sdph3_world_destroy(w);
    }

    /* a ball must roll */
    {
        sdph3_world *w = sdph3_world_create();
        sdph3_world_set_gravity(w, 0.0f, -9.81f, 0.0f);
        sdph3_add_box(w, 0, -1, 0, 20, 1, 20, 0.0f);
        sdph3_body *s = sdph3_add_sphere(w, 0, 0.5f, 0, 0.5f, 1.0f);
        sdph3_set_velocity(s, 4.0f, 0.0f, 0.0f);
        for (int i = 0; i < 120; ++i) sdph3_step(w, 1.0f / 60.0f);
        printf("    the ball: x %.3f, spin %.3f\n", s->pos[0], s->angvel[2]);
        check("the ball rolled forward", s->pos[0] > 3.0f);
        check("the ball spins the right way", s->angvel[2] < -1.0f);
        check("the ball does not spin on the wrong axis",
              fabsf(s->angvel[0]) < 0.05f && fabsf(s->angvel[1]) < 0.05f);
        sdph3_world_destroy(w);
    }

    /* the pointers must stay valid */
    {
        sdph3_world *w = sdph3_world_create();
        sdph3_body *a = sdph3_add_box(w, 0, 0, 0, 1, 1, 1, 1.0f);
        sdph3_body *b = sdph3_add_box(w, 3, 0, 0, 1, 1, 1, 1.0f);
        sdph3_body *c = sdph3_add_box(w, 6, 0, 0, 1, 1, 1, 1.0f);
        check("the pointers survive new bodies",
              a->pos[0] == 0.0f && b->pos[0] == 3.0f && c->pos[0] == 6.0f);
        sdph3_remove(w, b);
        check("the pointers survive a removal",
              a->pos[0] == 0.0f && c->pos[0] == 6.0f);
        sdph3_world_destroy(w);
    }

    /* a sleeping body must wake up */
    {
        sdph3_world *w = sdph3_world_create();
        sdph3_world_set_gravity(w, 0.0f, -9.81f, 0.0f);
        sdph3_add_box(w, 0, -1, 0, 20, 1, 20, 0.0f);
        sdph3_body *rest = sdph3_add_box(w, 0, 0.5f, 0, 0.5f, 0.5f, 0.5f, 1.0f);
        for (int i = 0; i < 300; ++i) sdph3_step(w, 1.0f / 60.0f);
        int was_asleep = rest->sleeping;
        sdph3_body *fall = sdph3_add_box(w, 0, 4.0f, 0, 0.5f, 0.5f, 0.5f, 1.0f);
        for (int i = 0; i < 180; ++i) sdph3_step(w, 1.0f / 60.0f);
        check("the resting body fell asleep", was_asleep == 1);
        check("the falling body stopped on the sleeper",
              fall->pos[1] > 0.9f && fall->pos[1] < 1.7f);
        sdph3_world_destroy(w);
    }
}

/* --------------------------------------------------------------- the 2D */

static void test_2d(void) {
    printf("2D:\n");

    {
        sdph2_world *w = sdph2_world_create();
        sdph2_add_box(w, 0, -1, 20, 1, 0.0f, 0.0f);
        sdph2_body *b = sdph2_add_box(w, 0, 3, 0.5f, 0.5f, 1.0f, 0.0f);
        for (int i = 0; i < 600; ++i) sdph2_step(w, 1.0f / 60.0f);
        printf("    the box: y %.3f, spin %.4f\n", b->pos[1], b->angvel);
        check("the box rests on the floor",
              b->pos[1] > 0.4f && b->pos[1] < 0.6f);
        check("the box does not spin", fabsf(b->angvel) < 0.01f);
        check("the box fell asleep", b->sleeping == 1);
        sdph2_world_destroy(w);
    }

    {
        sdph2_world *w = sdph2_world_create();
        sdph2_add_box(w, 0, -1, 20, 1, 0.0f, 0.0f);
        sdph2_body *s[4];
        for (int i = 0; i < 4; ++i)
            s[i] = sdph2_add_box(w, 0, 0.5f + i, 0.5f, 0.5f, 1.0f, 0.0f);
        for (int i = 0; i < 1200; ++i) sdph2_step(w, 1.0f / 60.0f);
        printf("    the stack: ");
        for (int i = 0; i < 4; ++i) printf("%.3f ", s[i]->pos[1]);
        printf("\n");
        check("the stack did not sink", s[0]->pos[1] > 0.42f);
        check("the stack did not drift", fabsf(s[3]->pos[0]) < 0.25f);
        sdph2_world_destroy(w);
    }

    {
        sdph2_world *w = sdph2_world_create();
        sdph2_body *a = sdph2_add_box(w, 0, 0, 1, 1, 1.0f, 0.0f);
        sdph2_body *b = sdph2_add_box(w, 3, 0, 1, 1, 1.0f, 0.0f);
        sdph2_add_box(w, 6, 0, 1, 1, 1.0f, 0.0f);
        check("the pointers survive new bodies",
              a->pos[0] == 0.0f && b->pos[0] == 3.0f);
        sdph2_remove(w, b);
        check("the pointers survive a removal", a->pos[0] == 0.0f);
        sdph2_world_destroy(w);
    }
}

int main(void) {
    printf("Silver Diamond - the physics tests\n\n");
    test_3d();
    test_2d();
    printf("\n%s\n", failed ? "FAILED" : "ALL PASS");
    return failed;
}
