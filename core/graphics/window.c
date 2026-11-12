/* core/graphics/window.c
 * Silver Diamond Game Engine :: engine-side window layer translation unit.
 *
 * window.h is header-only (GLFW/STB style): every user of the API just
 *     #include "window.h"
 * and THIS file is compiled once into the engine to instantiate it:
 *     #define SDGE_WINDOW_IMPLEMENTATION
 *     #include "window.h"
 *
 * The build script (build.py) compiles this file via core/config.py.
 */
#define SDGE_WINDOW_IMPLEMENTATION
#include "window.h"
