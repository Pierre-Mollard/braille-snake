#include "braille-snake.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define GAME_WIDTH 16
#define GAME_HEIGHT 4 // NOTE: for now only one line so must be 4

bool game_array[GAME_HEIGHT][GAME_WIDTH] = {0};
unsigned int player_pos_x = 0, player_pos_y = 2;
unsigned int player_speed = 1;
unsigned int player_length = 1;

void init_frame() {
  for (int x = 0; x < GAME_WIDTH; x++) {
    for (int y = 0; y < GAME_HEIGHT; y++) {
      game_array[y][x] = 0;
    }
  }
  game_array[player_pos_y][player_pos_x] = 1;
}

void update_frame() { game_array[player_pos_y][player_pos_x] = 1; }

void print_frame() {
  for (int i = 0; i < GAME_WIDTH; i += 2) {
    bool in_grid[4][2] = {0};
    in_grid[0][0] = game_array[0][i];
    in_grid[0][1] = game_array[0][i + 1];
    in_grid[1][0] = game_array[1][i];
    in_grid[1][1] = game_array[1][i + 1];
    in_grid[2][0] = game_array[2][i];
    in_grid[2][1] = game_array[2][i + 1];
    in_grid[3][0] = game_array[3][i];
    in_grid[3][1] = game_array[3][i + 1];
    unsigned char utf8_braille[4];
    int rc = encode_grid_to_braille(in_grid, utf8_braille);
    printf("%s", utf8_braille);
    if (rc != 0)
      printf("!");
  }
  printf("\n");
}

int main(int argc, char *argv[]) {
  printf("Welcome to BRAILLE SNAKE, press any key to start...\n");

  init_frame();

  print_frame();

  player_pos_x++;
  update_frame();
  print_frame();
  player_pos_x++;
  update_frame();
  print_frame();
  player_pos_y++;
  update_frame();
  print_frame();
  player_pos_x++;
  update_frame();
  print_frame();

  return EXIT_SUCCESS;
}
