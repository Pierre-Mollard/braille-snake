#include "braille-snake.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {

  printf("Testing braille characters: \n");

  uint32_t out_hexa = 0;

  bool in_grid[4][2] = {0};
  for (int x = 0; x < 2; x++) {
    for (int y = 0; y < 4; y++) {
      in_grid[y][x] = true;
      unsigned char out_utf8[4] = {0};
      encode_grid_to_braille(in_grid, out_utf8, &out_hexa);
      printf("%s, ", out_utf8);
    }
    printf("\n");
  }
  printf("\n");

  for (int i = 0; i < 256; i++) {
    bool in_grid[4][2] = {0};
    in_grid[0][0] = i & 0x01;
    in_grid[1][0] = i & 0x02;
    in_grid[2][0] = i & 0x04;
    in_grid[0][1] = i & 0x08;
    in_grid[1][1] = i & 0x10;
    in_grid[2][1] = i & 0x20;
    in_grid[3][0] = i & 0x40;
    in_grid[3][1] = i & 0x80;
    unsigned char out_utf8[4] = {0};
    encode_grid_to_braille(in_grid, out_utf8, &out_hexa);
    printf("%s ", out_utf8);
    if (i % 32 == 0)
      printf("\n");
  }
  printf("\n");

  return EXIT_SUCCESS;
}
