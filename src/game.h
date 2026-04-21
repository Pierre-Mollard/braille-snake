#pragma once

#include <stdbool.h>
#include <stdint.h>

#define BRAILLE_RATIO_H 4
#define BRAILLE_RATIO_W 2

struct player_cell {
  unsigned int pos_x;
  unsigned int pos_y;
  bool not_empty;
};

struct bonus_cell {
  unsigned int pos_x;
  unsigned int pos_y;
  unsigned int points;
  bool is_on_map;
};

typedef struct {
  int pos_x;
  int pos_y;
  int speed_x;
  int speed_y;
  int next_speed_x;
  int next_speed_y;
  unsigned int length;
  unsigned int score;
  unsigned int multiplier;
  unsigned int bonus_available_number;
  bool *game_array;
  struct player_cell *player_cells;
  struct bonus_cell *bonus_cells;
} Player;

typedef struct {
  int total_width;
  int total_height;
  int game_width;
  int game_height;
  unsigned int padding_height;
  unsigned int padding_width;
  bool god_mode;
  bool one_line_mode;
  bool simple_mode;
  bool slow_update; // only for tmux
  unsigned int max_concurrent_bonus;
  Player player;
} Game;

typedef enum {
  CMD_NONE,
  CMD_UP,
  CMD_DOWN,
  CMD_LEFT,
  CMD_RIGHT,
  CMD_RESTART,
  CMD_QUIT,
} Command;

typedef enum {
  GS_RUN,
  GS_LOSE,
  GS_WIN,
} GameState;

void game_init(Game *g, unsigned int total_width, unsigned int total_height);
void game_destroy(Game *g);
void game_reset(Game *g);
void game_handle_command(Game *g, Command cmd);
GameState game_tick(Game *g);
void spawn_goal(Game *g);
