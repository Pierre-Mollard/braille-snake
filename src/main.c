#include "braille-snake.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/poll.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define ESC_CHAR '\033'
#define ESC_SEP_CHAR '['
#define ESC "\033"
#define ESC_SEP "["
#define CSI ESC ESC_SEP
#define CURSOR_CMD "H"
#define CURSOR_HOME CSI CURSOR_CMD
#define CURSOR_TO_F CSI "%d;%d" CURSOR_CMD
#define CLEAR_SCREEN CSI "2J"
#define CLEAR_ALL CLEAR_SCREEN CURSOR_HOME
#define HIDE_CURSOR CSI "?25l"
#define SHOW_CURSOR CSI "?25h"
#define ALTERNATIVE_BUFFER_ON CSI "?1049h"
#define ALTERNATIVE_BUFFER_OFF CSI "?1049l"

#define GAME_WIDTH 64
#define GAME_HEIGHT 64 // for only one line must be 4
#define MAX_CONCURRENT_BONUS 3

bool game_array[GAME_HEIGHT][GAME_WIDTH] = {0};
unsigned int player_pos_x = 3, player_pos_y = 2;
int player_speed_x = 1;
int player_speed_y = 0;
int player_score = 0;
unsigned int player_length = 4;
unsigned int bonus_available_number = 0;
char input_display = ' ';

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

  // TODO: make win here (player is occupying all cells)
  if (player_length >= GAME_HEIGHT * GAME_WIDTH)
    return;

  // NOTE: max same time reached
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
      printf("index:%d, posX:%d, posY%d\n", index_free, rand_x, rand_y);
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
}

int update_frame() {
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
  if (game_array[player_cells[0].pos_y][player_cells[0].pos_x] == 1) {
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

void print_frame() {
  if (GAME_HEIGHT != 4)
    printf("\n");
  for (int j = 0; j < GAME_HEIGHT; j += 4) {
    for (int i = 0; i < GAME_WIDTH; i += 2) {
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
      int rc = encode_grid_to_braille(in_grid, utf8_braille);
      printf("%s", utf8_braille);
      if (rc != 0)
        printf("!");
    }
    if (GAME_HEIGHT != 4)
      printf("\n");
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
  printf("Welcome to BRAILLE SNAKE, press any key to start...\n");

  if (enable_raw_mode() == -1) {
    perror("enable_raw_mode");
    return 1;
  }
  atexit(restore_terminal);

  if (install_signal_handlers() == -1) {
    perror("sigaction");
    return 1;
  }

  srand(time(NULL));
  for (int i = 0; i < bonus_available_number; i++) {
    bonus_cells[i].is_on_map = false;
  }

  printf("%s", HIDE_CURSOR);
  init_frame();
  print_frame();

  struct pollfd poll_fd[1];
  poll_fd[0].fd = STDIN_FILENO;
  poll_fd[0].events = POLLIN;
  poll_fd[0].revents = 0;

  long long time_frame = 100;
  long long next_tick = now_ms() + time_frame;
  char input_display_content[20];

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
      if (!dead) {
        spawn_goal();
        printf("%s", CLEAR_ALL);
        printf("%s", input_display_content);
        print_frame();
        printf("[score:%d]", player_score);

        int bonus_count = 0;
        for (int i = 0; i < bonus_available_number; i++) {
          if (!bonus_cells[i].is_on_map)
            continue;
          bonus_count++;
          printf("\n[bonus#%d,x:%d,y:%d]", bonus_count, bonus_cells[i].pos_x,
                 bonus_cells[i].pos_y);
        }

      } else {
        printf("[GAMEOVER]\n");
        g_running = 0;
      }

      fflush(stdout);
      next_tick += time_frame;
    }
  }

  printf("%s", SHOW_CURSOR);
  printf("%s", CLEAR_ALL);
  fflush(stdout);
  return EXIT_SUCCESS;
}
