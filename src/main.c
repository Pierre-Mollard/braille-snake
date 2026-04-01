#include "braille-snake.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {

  printf("Welcome to BRAILLE SNAKE, press any key to start...\n");
  unsigned char test_value_utf8[4];
  int rc = encode_grid_to_braille(NULL, test_value_utf8);
  printf("returned rc=%d, <%s>\n", rc, test_value_utf8);
  return EXIT_SUCCESS;
}
