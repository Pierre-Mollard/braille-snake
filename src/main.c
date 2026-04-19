#include "braille-snake.h"
#include "game.h"
#include "mode-tty.h"
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

uint32_t utf8_symbol = ' ';

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
  printf("  -t            Enable tmux integration (run as server)\n");
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

  Game game = {0};

  bool user_tmux_mode = false;
  bool user_simple_mode = false;
  bool user_god_mode = false;
  bool user_one_line_mode = false;
  unsigned int user_total_height = 30;
  unsigned int user_total_width = 80;
  unsigned int user_max_bonus = 3;
  unsigned int user_multiplier = 1;
  unsigned int user_padding_height = 5;
  unsigned int user_padding_width = 2;
  char tmux_command_buf[64] = {0};

  int opt;
  while ((opt = getopt(argc, argv, "hl:c:gsof:m:t:")) != -1) {
    switch (opt) {
    case 'm':
      user_multiplier = atoi(optarg);
      if (user_multiplier <= 0)
        user_multiplier = 1;
      break;
    case 'c':
      user_total_width = atoi(optarg);
      if (user_total_width <= 0)
        user_total_width = 10;
      break;
    case 'l':
      user_total_height = atoi(optarg);
      if (user_total_height <= 0)
        user_total_height = 1;
      break;
    case 'f':
      user_max_bonus = atoi(optarg);
      if (user_max_bonus <= 0)
        user_max_bonus = 1;
      if (user_max_bonus > 10)
        user_max_bonus = 10;
      break;
    case 'g':
      user_god_mode = true;
      break;
    case 's':
      user_simple_mode = true;
      break;
    case 'o':
      user_one_line_mode = true;
      break;
    case 't':
      user_tmux_mode = true;
      strncpy(tmux_command_buf, optarg, sizeof(tmux_command_buf) - 1);
      tmux_command_buf[sizeof(tmux_command_buf) - 1] = '\0';
      break;
    case 'h':
      usage(argv[0]);
      return 1;
    }
  }

  game.padding_height = user_padding_height;
  game.padding_width = user_padding_width;
  game.total_height = user_total_height;
  game.total_width = user_total_width;
  game.god_mode = user_god_mode;
  game.simple_mode = user_simple_mode;
  game.one_line_mode = user_one_line_mode;
  game.max_concurrent_bonus = user_max_bonus;
  game.player.multiplier = user_multiplier;

  if (user_tmux_mode) {
    if (strcmp(tmux_command_buf, "") == 0) {
      perror("tmux command empty");
      return EXIT_FAILURE;
    }

    bool load_game = false;
    int return_status = tmux_server_mode(tmux_command_buf, &load_game);

    return return_status;
  }

  if (enable_raw_mode() == -1) {
    perror("enable_raw_mode");
    return 1;
  }
  atexit(restore_terminal);

  if (install_signal_handlers() == -1) {
    perror("sigaction");
    return 1;
  }

  struct pollfd poll_fd[1];
  poll_fd[0].fd = STDIN_FILENO;
  poll_fd[0].events = POLLIN;
  poll_fd[0].revents = 0;

  long long time_frame = 100;
  long long next_tick = now_ms() + time_frame;
  long long first_tick = now_ms();

  GameState game_state = GS_RUN;

  game_init(&game, user_total_width, user_total_height);
  tty_init(&game);
  game_render_tty_running(&game, time_frame, 0.0, utf8_symbol);
  game_handle_command(&game, CMD_RIGHT);

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
      Command command = CMD_RIGHT;
      // TODO: handle both input loops at once (r/q/play)
      if (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == '\x1b') {
          char seq[2];
          if (read(STDIN_FILENO, &seq[0], 1) > 0 &&
              read(STDIN_FILENO, &seq[1], 1) > 0) {
            if (seq[0] == '[') {
              switch (seq[1]) {
              case 'A': /* up */
                utf8_symbol = 0x2191;
                command = tty_input('k');
                break;
              case 'B': /* down */
                utf8_symbol = 0x2193;
                command = tty_input('j');
                break;
              case 'C': /* right */
                utf8_symbol = 0x2192;
                command = tty_input('l');
                break;
              case 'D': /* left */
                utf8_symbol = 0x2190;
                command = tty_input('h');
                break;
              }
            }
          }
        } else {
          /* normal character */
          utf8_symbol = c;
          command = tty_input(c);
        }
        if (command != CMD_NONE)
          game_handle_command(&game, command);
      }
    }

    if (ret_poll == 0 || now_ms() >= next_tick) {
      game_state = game_tick(&game);
      if (game_state == GS_RUN) {
        time_frame = 100 - (game.player.score / 5) * 5;
        if (time_frame < 40)
          time_frame = 40;
        spawn_goal(&game);
        double time_now = (now_ms() - first_tick) / 1000.0;
        game_render_tty_running(&game, time_frame, time_now, utf8_symbol);
      } else if (game_state == GS_LOSE) {
        game_render_tty_dead(&game);
        g_running = 0;
      } else if (game_state == GS_WIN) {
        game_render_tty_win(&game);
        g_running = 0;
      }

      next_tick += time_frame;
    }

    if (game_state != GS_RUN) {
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
            game.player.score = 0;
            game.player.pos_x = 3, game.player.pos_y = 2;
            game.player.speed_x = 1;
            game.player.speed_y = 0;
            game.player.length = 4;
            game.player.bonus_available_number = 0;
            first_tick = now_ms();
            game_tick(&game); // TODO: this broke
            break;
          }
        }
      }
    }
  }

  game_destroy(&game);
  tty_destroy(&game);
  return EXIT_SUCCESS;
}
