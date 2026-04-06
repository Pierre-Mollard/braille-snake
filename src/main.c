#include "braille-snake.h"
#include <bits/getopt_core.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define BRAILLE_RATIO_H 4
#define BRAILLE_RATIO_W 2

bool g_god_mode = false;
bool g_one_line_mode = false;
unsigned int g_multiplier = 1;

char *txt_game_title = "BRAILLE SNAKE";
char *txt_game_over_simple = "[GAMEOVER]";
char *txt_win_simple = "[WIN]";
char *txt_quit_details = "press q/Q/x/X/ESC to quit";
char *txt_reset_details = "press r/R to restart";
char *txt_game_over_one_line = "[GAMEOVER] (q/r?)";
char *txt_win_one_line = "[WIN] (q/r?)";

unsigned int player_pos_x = 3, player_pos_y = 2;
int player_speed_x = 1;
int player_speed_y = 0;
int player_score = 0;
unsigned int player_length = 4;
unsigned int bonus_available_number = 0;
uint32_t utf8_symbol = ' ';

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

bool *game_array;
struct player_cell *player_cells;
struct bonus_cell *bonus_cells;

static volatile sig_atomic_t g_running = 1;
static struct termios g_old_termios;

static void restore_terminal(void) {
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_old_termios);
}

static int enable_raw_mode(void) {
  struct termios raw;

  if (tcgetattr(STDIN_FILENO, &g_old_termios) == -1)
    return -1;

  raw = g_old_termios;
  raw.c_lflag &= ~(ICANON | ECHO);
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 0;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    return -1;

  return 0;
}

void spawn_goal(const unsigned int game_width, const unsigned int game_height,
                const unsigned int max_concurrent_bonus) {

  // check if any available place
  if (player_length + bonus_available_number >= game_height * game_width)
    return;

  // max same time reached
  if (bonus_available_number >= max_concurrent_bonus)
    return;

  // must be place (checked before)
  bool found_place = false;
  while (!found_place) {
    unsigned int rand_y = rand() % game_height;
    unsigned int rand_x = rand() % game_width;

    if (game_array[rand_y * game_width + rand_x] == 0) {
      // found place for spawn_goal
      int index_free = 0;
      while (bonus_cells[index_free].is_on_map) {
        index_free++;
        if (index_free > max_concurrent_bonus) {
          // if this happens big problem
          perror("index free moved past max possible");
        }
      }
      found_place = true;
      bonus_cells[index_free].pos_x = rand_x;
      bonus_cells[index_free].pos_y = rand_y;
      bonus_cells[index_free].points = 1;
      bonus_cells[index_free].is_on_map = true;
      bonus_available_number++;
    }
  }
}

void draw_edges(struct snake_ctx *ctx, unsigned int x, unsigned int y,
                unsigned int width, unsigned int height, bool simple_mode) {

  uint32_t hline = simple_mode ? '-' : 0x2550;
  uint32_t vline = simple_mode ? '|' : 0x2551;
  uint32_t top_left = simple_mode ? 'x' : 0x2554;
  uint32_t top_right = simple_mode ? 'x' : 0x2557;
  uint32_t bottom_left = simple_mode ? 'x' : 0x255A;
  uint32_t bottom_right = simple_mode ? 'x' : 0x255D;

  for (int i = x; i < x + width; i++) {
    put_utf8(ctx, hline, i, y);
    put_utf8(ctx, hline, i, height + y);
  }
  for (int i = y; i < y + height; i++) {
    put_utf8(ctx, vline, x, i);
    put_utf8(ctx, vline, x + width, i);
  }
  put_utf8(ctx, top_left, x, y);
  put_utf8(ctx, top_right, x + width, y);
  put_utf8(ctx, bottom_left, x, y + height);
  put_utf8(ctx, bottom_right, x + width, y + height);
}

int next_speed_x = 0;
int next_speed_y = 1;

void handle_user_input(char c) {
  int nx = next_speed_x;
  int ny = next_speed_y;

  if (c == 'k') {
    nx = 0;
    ny = -1;
  }
  if (c == 'j') {
    nx = 0;
    ny = 1;
  }
  if (c == 'l') {
    nx = 1;
    ny = 0;
  }
  if (c == 'h') {
    nx = -1;
    ny = 0;
  }

  if (nx == -player_speed_x && ny == -player_speed_y)
    return;

  next_speed_x = nx;
  next_speed_y = ny;
}

void init_frame(const unsigned int game_width, const unsigned int game_height) {
  srand(time(NULL));

  for (int x = 0; x < game_width; x++) {
    for (int y = 0; y < game_height; y++) {
      game_array[y * game_width + x] = 0;
    }
  }

  for (int i = 0; i < player_length; i++) {
    player_cells[i].pos_x = player_pos_x - i;
    player_cells[i].pos_y = player_pos_y;
    player_cells[i].not_empty = true;
  }

  for (int i = 0; i < bonus_available_number; i++) {
    bonus_cells[i].is_on_map = false;
  }
}

int update_frame(unsigned int game_width, unsigned int game_height) {

  player_speed_x = next_speed_x;
  player_speed_y = next_speed_y;

  if (player_length + bonus_available_number >= game_height * game_width)
    return 2;

  if (player_speed_x > 0) {
    player_pos_x = (player_pos_x + player_speed_x) % game_width;
  } else if (player_speed_x < 0) {
    unsigned int step = (unsigned int)(-player_speed_x);
    if (player_pos_x >= step) {
      player_pos_x -= step;
    } else {
      player_pos_x = game_width - (step - player_pos_x) % game_width;
      if (player_pos_x == game_width)
        player_pos_x = 0;
    }
  }

  if (player_speed_y > 0) {
    player_pos_y = (player_pos_y + player_speed_y) % game_height;
  } else if (player_speed_y < 0) {
    unsigned int step = (unsigned int)(-player_speed_y);
    if (player_pos_y >= step) {
      player_pos_y -= step;
    } else {
      player_pos_y = game_height - (step - player_pos_y) % game_height;
      if (player_pos_y == game_height)
        player_pos_y = 0;
    }
  }

  for (int i = 0; i < player_length; i++) {
    game_array[player_cells[i].pos_y * game_width + player_cells[i].pos_x] = 0;
  }

  for (int i = 0; i < bonus_available_number; i++) {
    if (bonus_cells[i].is_on_map)
      game_array[bonus_cells[i].pos_y * game_width + bonus_cells[i].pos_x] = 0;
  }

  unsigned int last_x = 0, last_y = 0;
  last_x = player_cells[player_length - 1].pos_x;
  last_y = player_cells[player_length - 1].pos_y;
  for (int i = player_length - 1; i > 0; i--) {
    player_cells[i].pos_x = player_cells[i - 1].pos_x;
    player_cells[i].pos_y = player_cells[i - 1].pos_y;
    game_array[player_cells[i].pos_y * game_width + player_cells[i].pos_x] = 1;
  }
  player_cells[0].pos_x = player_pos_x;
  player_cells[0].pos_y = player_pos_y;
  if (!g_god_mode &&
      game_array[player_cells[0].pos_y * game_width + player_cells[0].pos_x] ==
          1) {
    // NOTE: head is going in already occupied grid (not goal since not added
    // yet) so it has hit itself
    return 1;
  }
  game_array[player_cells[0].pos_y * game_width + player_cells[0].pos_x] = 1;

  int score_gained = 0;
  for (int i = 0; i < bonus_available_number; i++) {
    if (!bonus_cells[i].is_on_map)
      continue;

    if (bonus_cells[i].pos_x == player_pos_x &&
        bonus_cells[i].pos_y == player_pos_y) {
      bonus_cells[i].is_on_map = false;
      player_score += (bonus_cells[i].points * g_multiplier);
      score_gained++;
      player_cells[player_length].pos_x = last_x;
      player_cells[player_length].pos_y = last_y;
      player_cells[player_length].not_empty = true;
      player_length += g_multiplier;
      game_array[last_y * game_width + last_x] = 1;
    }
  }
  bonus_available_number -= score_gained;

  for (int i = 0; i < bonus_available_number; i++) {
    if (bonus_cells[i].is_on_map)
      game_array[bonus_cells[i].pos_y * game_width + bonus_cells[i].pos_x] = 1;
  }

  return 0;
}

void print_frame(struct snake_ctx *ctx, int offset_x, int offset_y,
                 unsigned int game_width, unsigned int game_height) {
  for (int j = 0, cell_y = 0; j < game_height; j += 4, cell_y++) {
    for (int i = 0, cell_x = 0; i < game_width; i += 2, cell_x++) {
      bool in_grid[4][2] = {0};
      in_grid[0][0] = game_array[j * game_width + i];
      in_grid[0][1] = game_array[j * game_width + i + 1];
      in_grid[1][0] = game_array[(j + 1) * game_width + i];
      in_grid[1][1] = game_array[(j + 1) * game_width + (i + 1)];
      in_grid[2][0] = game_array[(j + 2) * game_width + i];
      in_grid[2][1] = game_array[(j + 2) * game_width + (i + 1)];
      in_grid[3][0] = game_array[(j + 3) * game_width + i];
      in_grid[3][1] = game_array[(j + 3) * game_width + (i + 1)];
      unsigned char utf8_braille[4];
      uint32_t hexa_braille = 0;
      int rc = encode_grid_to_braille(in_grid, utf8_braille, &hexa_braille);
      size_t pos = (size_t)(cell_y + offset_y) * (size_t)ctx->nb_cols +
                   (size_t)(cell_x + offset_x);
      ctx->back_buffer[pos].symbol = hexa_braille;
      if (rc != 0)
        ctx->back_buffer[pos].symbol = '!';
    }
  }
}

static void handle_sigint(int signo) {
  (void)signo;
  g_running = 0;
}

static int install_signal_handlers(void) {
  struct sigaction sa;
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  if (sigaction(SIGINT, &sa, NULL) == -1)
    return -1;

  return 0;
}

static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

void usage(const char *prog_name) {
  printf("Usage: %s [options]\n", prog_name);
  printf("\n");
  printf("Braille Snake terminal game.\n");
  printf("One food = 1 growth + score points \n");
  printf("\n");
  printf("Options:\n");
  printf("  -h            Show this help and exit\n");
  printf("  -c <value>    Number of columns (min: 10)\n");
  printf("  -l <value>    Number of lines (min: 1)\n");
  printf(
      "  -f <value>    Max food spawned at the same time (min: 1, max: 10)\n");
  printf("  -g            Enable god mode\n");
  printf("  -s            Draw dashed border with fewer elements\n");
  printf("  -o            One-line mode (forces line count to 1)\n");
  printf("  -m <value>    Multiplier for score and growth (min:1)\n");
  printf("\n");
  printf("Examples:\n");
  printf("  %s -c 40 -l 20\n", prog_name);
  printf("  %s -c 60 -l 1 -o\n", prog_name);
  printf("  %s -f 5 -g\n", prog_name);
  printf("\n");
  printf("Controls:\n");
  printf("  h j k l / arrows      Move left/down/up/right\n");
  printf("  q/Q                   Quit\n");
}

int main(int argc, char *argv[]) {

  if (enable_raw_mode() == -1) {
    perror("enable_raw_mode");
    return 1;
  }
  atexit(restore_terminal);

  if (install_signal_handlers() == -1) {
    perror("sigaction");
    return 1;
  }

  bool simple_mode = false;
  bool god_mode = false;
  bool one_line_mode = false;
  unsigned int total_height = 30;
  unsigned int total_width = 80;

  unsigned int user_max_bonus = 0;
  unsigned int user_total_width = 0;
  unsigned int user_total_height = 0;

  const unsigned int padding_height = 5;
  const unsigned int padding_width = 2;
  unsigned int max_concurrent_bonus = 3;

  int opt;
  while ((opt = getopt(argc, argv, "hl:c:gsof:m:")) != -1) {
    switch (opt) {
    case 'm':
      g_multiplier = atoi(optarg);
      if (g_multiplier <= 0)
        g_multiplier = 1;
      break;
    case 'c':
      user_total_width = atoi(optarg);
      if (user_total_width <= 0)
        user_total_width = 10;
      total_width = user_total_width;
      break;
    case 'l':
      user_total_height = atoi(optarg);
      if (user_total_height <= 0)
        user_total_height = 1;
      total_height = user_total_height;
      break;
    case 'f':
      user_max_bonus = atoi(optarg);
      if (user_max_bonus <= 0)
        user_max_bonus = 1;
      if (user_max_bonus > 10)
        user_max_bonus = 10;
      max_concurrent_bonus = user_max_bonus;
      break;
    case 'g':
      god_mode = true;
      break;
    case 's':
      simple_mode = true;
      break;
    case 'o':
      one_line_mode = true;
      break;
    case 'h':
      usage(argv[0]);
      return 1;
    }
  }

  unsigned int game_height = (total_height - padding_height) * BRAILLE_RATIO_H;
  unsigned int game_width = (total_width - padding_width) * BRAILLE_RATIO_W;
  if (one_line_mode) {
    game_height = 4;
    total_height = 1;
  }
  g_god_mode = god_mode;
  g_one_line_mode = one_line_mode;

  game_array = calloc(game_width * game_height, sizeof(bool));
  player_cells = calloc(game_width * game_height, sizeof(*player_cells));
  bonus_cells = calloc(max_concurrent_bonus, sizeof(*bonus_cells));

  struct snake_ctx ctx = {0};
  create_buffers(&ctx, 100, 100);

  struct pollfd poll_fd[1];
  poll_fd[0].fd = STDIN_FILENO;
  poll_fd[0].events = POLLIN;
  poll_fd[0].revents = 0;

  long long time_frame = 100;
  long long next_tick = now_ms() + time_frame;
  long long first_tick = now_ms();
  int dead = 0;

  char input_display_content[20];
  char output_display_content[40];
  char output_time_content[40];
  char output_score_content[40];

  draw_first(&ctx);

  init_frame(game_width, game_height);

  draw_full(&ctx);
  print_frame(&ctx, 1, 3, game_width, game_height);

  while (g_running) {
    long long ms_left = next_tick - now_ms();
    if (ms_left < 0)
      ms_left = 0;

    int ret_poll = poll(poll_fd, 1, (int)ms_left);

    if (ret_poll == -1) {
      if (errno == EINTR)
        continue;
      perror("poll");
      break;
    }

    if (ret_poll > 0 && (poll_fd[0].revents & POLLIN)) {

      char c;
      if (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == '\x1b') {
          char seq[2];
          if (read(STDIN_FILENO, &seq[0], 1) > 0 &&
              read(STDIN_FILENO, &seq[1], 1) > 0) {
            if (seq[0] == '[') {
              switch (seq[1]) {
              case 'A': /* up */
                utf8_symbol = 0x2191;
                handle_user_input('k');
                break;
              case 'B': /* down */
                utf8_symbol = 0x2193;
                handle_user_input('j');
                break;
              case 'C': /* right */
                utf8_symbol = 0x2192;
                handle_user_input('l');
                break;
              case 'D': /* left */
                utf8_symbol = 0x2190;
                handle_user_input('h');
                break;
              }
            }
          }
        } else {
          /* normal character */
          utf8_symbol = c;
          handle_user_input(c);
        }
      }
    }

    if (ret_poll == 0 || now_ms() >= next_tick) {
      dead = update_frame(game_width, game_height);
      if (dead == 0) {

        time_frame = 100 - (player_score / 5) * 5;
        if (time_frame < 40)
          time_frame = 40;

        spawn_goal(game_width, game_height, max_concurrent_bonus);
        clear_everything(&ctx);
        if (!one_line_mode) {
          int local_room_used = -2;
          put_str(&ctx, txt_game_title, strlen(txt_game_title),
                  total_width / 2 - (strlen(txt_game_title) / 2), 0);

          snprintf(output_score_content, sizeof(output_score_content),
                   "[score:%3d]", player_score);
          local_room_used += strlen(output_score_content);
          if (local_room_used < total_width)
            put_str(&ctx, output_score_content, strlen(output_score_content),
                    total_width - strlen(output_score_content) + 1, 1);

          snprintf(output_display_content, sizeof(output_display_content),
                   "[x%d]", g_multiplier);
          local_room_used += strlen(output_display_content);
          if (local_room_used < total_width)
            put_str(&ctx, output_display_content,
                    strlen(output_display_content),
                    total_width - (strlen(output_display_content) +
                                   strlen(output_score_content) - 1),
                    1);

          snprintf(input_display_content, sizeof(input_display_content),
                   "[input: ]");
          local_room_used += strlen(input_display_content);
          if (local_room_used < total_width) {
            put_str(&ctx, input_display_content, strlen(input_display_content),
                    0, 1);
            put_utf8(&ctx, utf8_symbol, 7, 1);
          }

          draw_edges(&ctx, 0, 2, total_width - padding_width + 2,
                     total_height - padding_height + 1, simple_mode);
          print_frame(&ctx, 1, 3, game_width, game_height);

          local_room_used = -1;
          snprintf(output_display_content, sizeof(output_display_content),
                   "[speed:x=%2d,y=%2d]", player_speed_x, player_speed_y);
          local_room_used += strlen(output_display_content);
          if (local_room_used < total_width) {
            put_str(&ctx, output_display_content,
                    strlen(output_display_content), 0, total_height - 1);
          }
          if (god_mode) {
            local_room_used += strlen("[god_mode]");
            if (local_room_used < total_width)
              put_str(&ctx, "[god_mode]", strlen(output_display_content),
                      strlen(output_display_content), total_height - 1);
          }
          snprintf(output_display_content, sizeof(output_display_content),
                   "[time:%4.1f]", (now_ms() - first_tick) / 1000.0);
          local_room_used += strlen(output_display_content);
          if (local_room_used < total_width)
            put_str(
                &ctx, output_display_content, strlen(output_display_content),
                total_width - strlen(output_display_content), total_height - 1);

          snprintf(output_time_content, sizeof(output_time_content),
                   "[updates:%lldms]", time_frame);
          local_room_used += strlen(output_time_content);
          if (local_room_used < total_width)
            put_str(&ctx, output_time_content, strlen(output_time_content),
                    total_width - strlen(output_display_content) -
                        strlen(output_time_content),
                    total_height - 1);
        } else {
          int local_offset = 0;
          if (!simple_mode) {
            snprintf(input_display_content, sizeof(input_display_content),
                     "[input: ]");
            put_str(&ctx, input_display_content, strlen(input_display_content),
                    0, 0);
            local_offset = 9;
          }
          put_utf8(&ctx, utf8_symbol, local_offset - 2, 0);
          print_frame(&ctx, local_offset, 0, game_width, game_height);
          snprintf(output_score_content, sizeof(output_score_content),
                   "[score:%3d]", player_score);
          put_str(&ctx, output_score_content, strlen(output_score_content),
                  game_width / 2 + local_offset, 0);
        }

      } else if (dead == 1) {
        if (!one_line_mode) {
          if (total_width > strlen(txt_quit_details)) {
            put_str(&ctx, txt_game_over_simple, strlen(txt_game_over_simple),
                    total_width / 2 - (strlen(txt_game_over_simple) / 2),
                    total_height / 2 - 1);
            put_str(&ctx, txt_quit_details, strlen(txt_quit_details),
                    total_width / 2 - (strlen(txt_quit_details) / 2),
                    total_height / 2);
            put_str(&ctx, txt_reset_details, strlen(txt_reset_details),
                    total_width / 2 - (strlen(txt_reset_details) / 2),
                    total_height / 2 + 4);
          } else {
            put_str(&ctx, txt_game_over_one_line,
                    strlen(txt_game_over_one_line),
                    total_width / 2 - (strlen(txt_game_over_one_line) / 2),
                    total_height / 2 - 1);
          }
        } else {
          put_str(&ctx, txt_game_over_one_line, strlen(txt_game_over_one_line),
                  total_width, 0);
        }
        g_running = 0;
      } else if (dead == 2) {
        if (!one_line_mode) {
          if (total_width > strlen(txt_quit_details)) {
            put_str(&ctx, txt_win_simple, strlen(txt_win_simple),
                    total_width / 2 - (strlen(txt_win_simple) / 2),
                    total_height / 2 - 1);
            put_str(&ctx, txt_quit_details, strlen(txt_quit_details),
                    total_width / 2 - (strlen(txt_quit_details) / 2),
                    total_height / 2);
            put_str(&ctx, txt_reset_details, strlen(txt_reset_details),
                    total_width / 2 - (strlen(txt_reset_details) / 2),
                    total_height / 2 + 4);
          } else {
            put_str(&ctx, txt_win_one_line, strlen(txt_win_one_line),
                    total_width / 2 - (strlen(txt_win_one_line) / 2),
                    total_height / 2 - 1);
          }
        } else {
          put_str(&ctx, txt_win_one_line, strlen(txt_win_one_line), total_width,
                  0);
        }
        g_running = 0;
      }

      draw_diff(&ctx);
      next_tick += time_frame;
    }

    if (dead != 0) {
      char last_char_to_end = 0;
      ssize_t n = 0;
      while (true) {
        n = read(STDIN_FILENO, &last_char_to_end, 1);
        if (n == 1) {
          if (last_char_to_end == 'q' || last_char_to_end == 'Q' ||
              last_char_to_end == 'x' || last_char_to_end == 'X') {
            break;
          } else if (last_char_to_end == '\x1b') {
            char seq[2];
            ssize_t n1 = read(STDIN_FILENO, &seq[0], 1);
            ssize_t n2 = read(STDIN_FILENO, &seq[1], 1);

            if (n1 == 1 && n2 == 1 && seq[0] == '[' &&
                (seq[1] == 'A' || seq[1] == 'B' || seq[1] == 'C' ||
                 seq[1] == 'D')) {
              continue; // arrow key, do not quit
            }

            break;
          } else if (last_char_to_end == 'r' || last_char_to_end == 'R') {
            last_char_to_end = 0;
            g_running = 1;
            player_score = 0;
            player_pos_x = 3, player_pos_y = 2;
            player_speed_x = 1;
            player_speed_y = 0;
            init_frame(game_width, game_height);
            player_length = 4;
            bonus_available_number = 0;
            first_tick = now_ms();
            break;
          }
        }
      }
    }
  }

  draw_last(&ctx);
  free_buffers(&ctx);
  free(game_array);
  free(player_cells);
  free(bonus_cells);
  return EXIT_SUCCESS;
}
