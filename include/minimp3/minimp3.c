/* include/minimp3/minimp3.c
 *
 * Third-party translation unit that instantiates the minimp3 decoder.
 * minimp3 is public domain (CC0 1.0) - https://github.com/lieff/minimp3
 *
 * Compiled into the engine library only (see build.py: lib_c_files).
 * The public API (mp3dec_*) is declared in minimp3.h; this file provides
 * the implementation for exactly one TU.
 */
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"
