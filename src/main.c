#include "braille-snake.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define GAME_WIDTH 64
#define GAME_HEIGHT 32 // for only one line must be 4
#define MAX_CONCURRENT_BONUS 3

bool game_array[GAME_HEIGHT][GAME_WIDTH] = {0};
unsigned int player_pos_x = 3, player_pos_y = 2;
int player_speed_x = 1;
int player_speed_y = 0;
int player_score = 0;
unsigned int player_length = 4;
unsigned int bonus_available_number = 0;
char input_display = ' ';
bool god_mode = false;

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

struct player_cell player_cells[GAME_HEIGHT * GAME_WIDTH] = {0};
struct bonus_cell bonus_cells[MAX_CONCURRENT_BONUS] = {0};

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

void spawn_goal() {

  // check if any available place
  if (player_length + bonus_available_number >= GAME_HEIGHT * GAME_WIDTH)
    return;

  // max same time reached
  if (bonus_available_number >= MAX_CONCURRENT_BONUS)
    return;

  // must be place (checked before)
  bool found_place = false;
  while (!found_place) {
    unsigned int rand_y = rand() % GAME_HEIGHT;
    unsigned int rand_x = rand() % GAME_WIDTH;

    if (game_array[rand_y][rand_x] == 0) {
      // found place for spawn_goal
      int index_free = 0;
      while (bonus_cells[index_free].is_on_map) {
        index_free++;
        if (index_free > MAX_CONCURRENT_BONUS) {
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

void handle_user_input(char c) {
  if (c == 'k' && player_speed_x != 0) {
    player_speed_x = 0;
    player_speed_y = -1;
  }
  if (c == 'j' && player_speed_x != 0) {
    player_speed_x = 0;
    player_speed_y = 1;
  }
  if (c == 'l' && player_speed_y != 0) {
    player_speed_x = 1;
    player_speed_y = 0;
  }
  if (c == 'h' && player_speed_y != 0) {
    player_speed_x = -1;
    player_speed_y = 0;
  }

  input_display = c;
}

void init_frame() {
  srand(time(NULL));

  for (int x = 0; x < GAME_WIDTH; x++) {
    for (int y = 0; y < GAME_HEIGHT; y++) {
      game_array[y][x] = 0;
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

int update_frame() {

  if (player_length + bonus_available_number >= GAME_HEIGHT * GAME_WIDTH)
    return 2;

  player_pos_x = (player_pos_x + player_speed_x) % GAME_WIDTH;
  player_pos_y = (player_pos_y + player_speed_y) % GAME_HEIGHT;

  for (int i = 0; i < player_length; i++) {
    game_array[player_cells[i].pos_y][player_cells[i].pos_x] = 0;
  }

  for (int i = 0; i < bonus_available_number; i++) {
    if (bonus_cells[i].is_on_map)
      game_array[bonus_cells[i].pos_y][bonus_cells[i].pos_x] = 0;
  }

  unsigned int last_x = 0, last_y = 0;
  last_x = player_cells[player_length - 1].pos_x;
  last_y = player_cells[player_length - 1].pos_y;
  for (int i = player_length - 1; i > 0; i--) {
    player_cells[i].pos_x = player_cells[i - 1].pos_x;
    player_cells[i].pos_y = player_cells[i - 1].pos_y;
    game_array[player_cells[i].pos_y][player_cells[i].pos_x] = 1;
  }
  player_cells[0].pos_x = player_pos_x;
  player_cells[0].pos_y = player_pos_y;
  if (!god_mode &&
      game_array[player_cells[0].pos_y][player_cells[0].pos_x] == 1) {
    // NOTE: head is going in already occupied grid (not goal since not added
    // yet) so it has hit itself
    return 1;
  }
  game_array[player_cells[0].pos_y][player_cells[0].pos_x] = 1;

  int score_gained = 0;
  for (int i = 0; i < bonus_available_number; i++) {
    if (!bonus_cells[i].is_on_map)
      continue;

    if (bonus_cells[i].pos_x == player_pos_x &&
        bonus_cells[i].pos_y == player_pos_y) {
      bonus_cells[i].is_on_map = false;
      player_score += bonus_cells[i].points;
      score_gained++;
      player_cells[player_length].pos_x = last_x;
      player_cells[player_length].pos_y = last_y;
      player_cells[player_length].not_empty = true;
      player_length++;
      game_array[last_y][last_x] = 1;
    }
  }
  bonus_available_number -= score_gained;

  for (int i = 0; i < bonus_available_number; i++) {
    if (bonus_cells[i].is_on_map)
      game_array[bonus_cells[i].pos_y][bonus_cells[i].pos_x] = 1;
  }

  return 0;
}

void print_frame(struct snake_ctx *ctx, int offset_x, int offset_y) {
  for (int j = 0, cell_y = 0; j < GAME_HEIGHT; j += 4, cell_y++) {
    for (int i = 0, cell_x = 0; i < GAME_WIDTH; i += 2, cell_x++) {
      bool in_grid[4][2] = {0};
      in_grid[0][0] = game_array[j][i];
      in_grid[0][1] = game_array[j][i + 1];
      in_grid[1][0] = game_array[j + 1][i];
      in_grid[1][1] = game_array[j + 1][i + 1];
      in_grid[2][0] = game_array[j + 2][i];
      in_grid[2][1] = game_array[j + 2][i + 1];
      in_grid[3][0] = game_array[j + 3][i];
      in_grid[3][1] = game_array[j + 3][i + 1];
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

  struct snake_ctx ctx = {0};
  create_buffers(&ctx, 100, 100);

  draw_first(&ctx);

  init_frame();

  draw_full(&ctx);
  print_frame(&ctx, 0, 1);

  struct pollfd poll_fd[1];
  poll_fd[0].fd = STDIN_FILENO;
  poll_fd[0].events = POLLIN;
  poll_fd[0].revents = 0;

  long long time_frame = 100;
  long long next_tick = now_ms() + time_frame;
  long long first_tick = now_ms();

  char input_display_content[20];
  char output_display_content[40];
  char output_score_content[40];
  sprintf(input_display_content, "[input:     ]");

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
                sprintf(input_display_content, "[input:  UP ]");
                handle_user_input('k');
                break;
              case 'B': /* down */
                sprintf(input_display_content, "[input: DOWN]");
                handle_user_input('j');
                break;
              case 'C': /* right */
                sprintf(input_display_content, "[input:RIGHT]");
                handle_user_input('l');
                break;
              case 'D': /* left */
                sprintf(input_display_content, "[input: LEFT]");
                handle_user_input('h');
                break;
              }
            }
          }
        } else {
          /* normal character */
          sprintf(input_display_content, "[input:  %c  ]", c);
          handle_user_input(c);
        }
      }
    }

    if (ret_poll == 0 || now_ms() >= next_tick) {
      int dead = update_frame();
      if (dead == 0) {
        spawn_goal();
        clear_everything(&ctx);
        put_str(&ctx, input_display_content, strlen(input_display_content), 0,
                0);
        snprintf(output_score_content, sizeof(output_score_content),
                 "[score:%d]", player_score);
        put_str(&ctx, output_score_content, strlen(output_score_content), 50,
                0);
        print_frame(&ctx, 0, 1);
        snprintf(output_display_content, sizeof(output_display_content),
                 "[speed:x=%d,y=%d]", player_speed_x, player_speed_y);
        put_str(&ctx, output_display_content, strlen(output_display_content),
                50, 22);
        snprintf(output_display_content, sizeof(output_display_content),
                 "[time:%f]", (now_ms() - first_tick) / 1000.0);
        put_str(&ctx, output_display_content, strlen(output_display_content),
                50, 23);

        int bonus_count = 0;
        for (int i = 0; i < bonus_available_number; i++) {
          if (!bonus_cells[i].is_on_map)
            continue;
          bonus_count++;
          snprintf(output_display_content, sizeof(output_display_content),
                   "[bonus#%d,x:%d,y:%d]", bonus_count, bonus_cells[i].pos_x,
                   bonus_cells[i].pos_y);
          put_str(&ctx, output_display_content, strlen(output_display_content),
                  0, 22 + i);
        }

      } else if (dead == 1) {
        put_str(&ctx, "[GAMEOVER]", sizeof("[GAMEOVER]"), 50, 22);
        g_running = 0;
      } else if (dead == 2) {
        put_str(&ctx, "[WIN]", sizeof("[WIN]"), 50, 22);
        g_running = 0;
      }

      draw_diff(&ctx);
      next_tick += time_frame;
    }
  }

  // TODO: (not working) put a way to quit only after use read score and stuff
  char last_char_to_end = ' ';
  read(STDIN_FILENO, &last_char_to_end, 1);

  draw_last(&ctx);
  free_buffers(&ctx);
  return EXIT_SUCCESS;
}
