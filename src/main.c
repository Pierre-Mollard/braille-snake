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

#include "game.h"
#include "mode-tmux.h"
#include "mode-tty.h"

static volatile sig_atomic_t g_running = 1;

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

int app_is_running(void) { return g_running; }

long long now_ms(void) {
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

      // override user params in tmux mode
      user_one_line_mode = true;
      user_simple_mode = true;
      user_total_width = 6;
      user_max_bonus = 1;
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
    if (tmux_command_buf[0] == '\0') {
      fprintf(stderr, "tmux command empty\n");
      return EXIT_FAILURE;
    }

    tmux_command_type cmd_type = TMCD_ERROR;
    int rc = tmux_user_out_cmd(tmux_command_buf, &cmd_type);
    if (rc != EXIT_SUCCESS && cmd_type == TMCD_UNDEF) {
      printf("tmux_handle_command not found\n");
      return EXIT_FAILURE;
    }
    if (rc != EXIT_SUCCESS || cmd_type == TMCD_ERROR) {
      // print inside function used, printf("tmux_handle_command failed\n");
      return EXIT_FAILURE;
    }
    if (rc == EXIT_SUCCESS && cmd_type == TMCD_CLIENT)
      return EXIT_SUCCESS;
  }

  if (install_signal_handlers() == -1) {
    perror("sigaction");
    return EXIT_FAILURE;
  }

  game_init(&game, user_total_width, user_total_height);

  if (user_tmux_mode) {
    run_tmux_mode(&game);
  } else {
    run_tty_mode(&game);
  }

  game_destroy(&game);
  return EXIT_SUCCESS;
}
