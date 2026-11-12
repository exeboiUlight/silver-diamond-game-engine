/* sdge_def.h - Silver Diamond Game Engine :: common defines
 *
 * Export macro for the engine shared library (sdge.dll / libsdge.so):
 *   - compile the DLL itself    with -DSDGE_BUILD_DLL  -> __declspec(dllexport)
 *   - consume the DLL from C    with -DSDGE_USE_DLL    -> __declspec(dllimport)
 *   - static / header-only use (tests, tools)           -> empty macro
 *
 * SDGE_INLINE: internal linkage for header-only helpers (safe in C99).
 */
#ifndef SDGE_DEF_H
#define SDGE_DEF_H

#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
  #if defined(SDGE_BUILD_DLL)
    #define SDGE_API __declspec(dllexport)
  #elif defined(SDGE_USE_DLL)
    #define SDGE_API __declspec(dllimport)
  #else
    #define SDGE_API
  #endif
#else
  #define SDGE_API __attribute__((visibility("default")))
#endif

/* SDGE_INLINE: internal linkage for header-only helpers (safe in C99;
 * `static inline` also silences -Wunused-function in TUs that use a subset) */
#define SDGE_INLINE static inline

#define SDGE_VERSION_STRING  "Silver Diamond Game Engine 0.2"

#endif /* SDGE_DEF_H */
