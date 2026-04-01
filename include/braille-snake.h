#pragma once
#include <stdbool.h>

int encode_grid_to_braille(bool in_grid[4][2], unsigned char out_utf8[4]);
