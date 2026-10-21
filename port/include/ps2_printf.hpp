#pragma once

// Included by port.h in ct_ps2's units (CT_PS2_UNIT): the game's printf still reaches stdout, and each line
// it completes is logged under the ps2 category (port/src/log/ps2_printf.cpp). The host's <cstdio> is read
// first so its own declarations keep their names.

#include <cstdio>

/**
 * Writes game printf output to stdout and the PS2 logging category.
 */
int CtPs2Printf(const char *format, ...);

#define printf(...) CtPs2Printf(__VA_ARGS__)
