#include "game.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void game_reset(Game *g) {
  Player *p = &g->player;
  size_t game_size = (size_t)g->game_width * (size_t)g->game_height;

  memset(p->game_array, 0, game_size * sizeof(*p->game_array));
  memset(p->player_cells, 0, game_size * sizeof(*p->player_cells));
  memset(p->bonus_cells, 0, g->max_concurrent_bonus * sizeof(*p->bonus_cells));

  p->pos_x = 3;
  p->pos_y = 2;

  p->speed_x = 1;
  p->speed_y = 0;
  p->next_speed_x = 1;
  p->next_speed_y = 0;

  p->length = 4;
  p->score = 0;

  for (unsigned int i = 0; i < p->length; i++) {
    p->player_cells[i].pos_x = p->pos_x - i;
    p->player_cells[i].pos_y = p->pos_y;
    p->player_cells[i].not_empty = true;
    p->game_array[p->player_cells[i].pos_y * g->game_width +
                  p->player_cells[i].pos_x] = 1;
  }

  for (unsigned int i = 0; i < g->max_concurrent_bonus; i++) {
    p->bonus_cells[i].is_on_map = false;
  }

  p->bonus_available_number = 0;
}

void game_init(Game *g, unsigned int total_width, unsigned int total_height) {

  g->total_height = total_height;
  g->total_width = total_width;

  if (g->simple_mode && g->one_line_mode) {
    g->padding_height = 0;
    g->padding_width = 0;
  }

  unsigned int game_height =
      (total_height - g->padding_height) * BRAILLE_RATIO_H;
  unsigned int game_width = (total_width - g->padding_width) * BRAILLE_RATIO_W;
  if (g->one_line_mode) {
    game_height = 4;
    g->total_height = 1;
  }
  g->game_height = game_height;
  g->game_width = game_width;

  size_t game_size = g->game_width * g->game_height;
  g->player.game_array = calloc(game_size, sizeof(bool));
  g->player.player_cells = calloc(game_size, sizeof(*g->player.player_cells));
  g->player.bonus_cells =
      calloc(g->max_concurrent_bonus, sizeof(*g->player.bonus_cells));

  srand(time(NULL));
  game_reset(g);

  for (int x = 0; x < game_width; x++) {
    for (int y = 0; y < game_height; y++) {
      g->player.game_array[y * game_width + x] = 0;
    }
  }

  for (int i = 0; i < g->player.length; i++) {
    g->player.player_cells[i].pos_x = g->player.pos_x - i;
    g->player.player_cells[i].pos_y = g->player.pos_y;
    g->player.player_cells[i].not_empty = true;
  }

  for (int i = 0; i < g->player.bonus_available_number; i++) {
    g->player.bonus_cells[i].is_on_map = false;
  }
}

void game_destroy(Game *g) {
  free(g->player.game_array);
  free(g->player.player_cells);
  free(g->player.bonus_cells);
  g->player.game_array = NULL;
  g->player.player_cells = NULL;
  g->player.bonus_cells = NULL;
}

void game_handle_command(Game *g, Command cmd) {

  int nx = g->player.next_speed_x;
  int ny = g->player.next_speed_y;

  if (cmd == CMD_UP) {
    nx = 0;
    ny = -1;
  }
  if (cmd == CMD_DOWN) {
    nx = 0;
    ny = 1;
  }
  if (cmd == CMD_RIGHT) {
    nx = 1;
    ny = 0;
  }
  if (cmd == CMD_LEFT) {
    nx = -1;
    ny = 0;
  }

  if (nx == -g->player.speed_x && ny == -g->player.speed_y)
    return;

  g->player.next_speed_x = nx;
  g->player.next_speed_y = ny;
}

GameState game_tick(Game *g) {

  int game_width = g->game_width;
  int game_height = g->game_height;

  Player *player = &g->player;
  player->speed_x = player->next_speed_x;
  player->speed_y = player->next_speed_y;

  if (player->length + player->bonus_available_number >=
      game_height * game_width)
    return GS_WIN;

  if (player->speed_x > 0) {
    player->pos_x = (player->pos_x + player->speed_x) % game_width;
  } else if (player->speed_x < 0) {
    unsigned int step = (unsigned int)(-player->speed_x);
    if (player->pos_x >= step) {
      player->pos_x -= step;
    } else {
      player->pos_x = game_width - (step - player->pos_x) % game_width;
      if (player->pos_x == game_width)
        player->pos_x = 0;
    }
  }

  if (player->speed_y > 0) {
    player->pos_y = (player->pos_y + player->speed_y) % game_height;
  } else if (player->speed_y < 0) {
    unsigned int step = (unsigned int)(-player->speed_y);
    if (player->pos_y >= step) {
      player->pos_y -= step;
    } else {
      player->pos_y = game_height - (step - player->pos_y) % game_height;
      if (player->pos_y == game_height)
        player->pos_y = 0;
    }
  }

  for (int i = 0; i < player->length; i++) {
    player->game_array[player->player_cells[i].pos_y * game_width +
                       player->player_cells[i].pos_x] = 0;
  }

  for (int i = 0; i < player->bonus_available_number; i++) {
    if (player->bonus_cells[i].is_on_map)
      player->game_array[player->bonus_cells[i].pos_y * game_width +
                         player->bonus_cells[i].pos_x] = 0;
  }

  unsigned int last_x = 0, last_y = 0;
  last_x = player->player_cells[player->length - 1].pos_x;
  last_y = player->player_cells[player->length - 1].pos_y;
  for (int i = player->length - 1; i > 0; i--) {
    player->player_cells[i].pos_x = player->player_cells[i - 1].pos_x;
    player->player_cells[i].pos_y = player->player_cells[i - 1].pos_y;
    player->game_array[player->player_cells[i].pos_y * game_width +
                       player->player_cells[i].pos_x] = 1;
  }
  player->player_cells[0].pos_x = player->pos_x;
  player->player_cells[0].pos_y = player->pos_y;
  if (!g->god_mode &&
      player->game_array[player->player_cells[0].pos_y * game_width +
                         player->player_cells[0].pos_x] == 1) {
    // NOTE: head is going in already occupied grid (not goal since not added
    // yet) so it has hit itself
    return GS_LOSE;
  }
  player->game_array[player->player_cells[0].pos_y * game_width +
                     player->player_cells[0].pos_x] = 1;

  int score_gained = 0;
  for (int i = 0; i < player->bonus_available_number; i++) {
    if (!player->bonus_cells[i].is_on_map)
      continue;

    if (player->bonus_cells[i].pos_x == player->pos_x &&
        player->bonus_cells[i].pos_y == player->pos_y) {
      player->bonus_cells[i].is_on_map = false;
      player->score += (player->bonus_cells[i].points * player->multiplier);
      score_gained++;
      player->player_cells[player->length].pos_x = last_x;
      player->player_cells[player->length].pos_y = last_y;
      player->player_cells[player->length].not_empty = true;
      player->length += player->multiplier;
      player->game_array[last_y * game_width + last_x] = 1;
    }
  }
  player->bonus_available_number -= score_gained;

  for (int i = 0; i < player->bonus_available_number; i++) {
    if (player->bonus_cells[i].is_on_map)
      player->game_array[player->bonus_cells[i].pos_y * game_width +
                         player->bonus_cells[i].pos_x] = 1;
  }

  return GS_RUN;
}

void spawn_goal(Game *g) {

  int game_width = g->game_width;
  int game_height = g->game_height;
  Player *player = &g->player;

  // check if any available place
  if (player->length + player->bonus_available_number >=
      game_height * game_width)
    return;

  // max same time reached
  if (player->bonus_available_number >= g->max_concurrent_bonus)
    return;

  // must be place (checked before)
  bool found_place = false;
  while (!found_place) {
    unsigned int rand_y = rand() % game_height;
    unsigned int rand_x = rand() % game_width;

    if (player->game_array[rand_y * game_width + rand_x] == 0) {
      // found place for spawn_goal
      int index_free = 0;
      while (player->bonus_cells[index_free].is_on_map) {
        index_free++;
        if (index_free > g->max_concurrent_bonus) {
          // if this happens big problem
          perror("index free moved past max possible");
        }
      }
      found_place = true;
      player->bonus_cells[index_free].pos_x = rand_x;
      player->bonus_cells[index_free].pos_y = rand_y;
      player->bonus_cells[index_free].points = 1;
      player->bonus_cells[index_free].is_on_map = true;
      player->bonus_available_number++;
    }
  }
}
