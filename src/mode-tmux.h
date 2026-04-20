#pragma once

#include "game.h"
#include <stddef.h>

// utf8 can have 4 bytes per char at most
#define UTF8_RES 4

typedef struct {
  int fd_lock;
  int fd_sock;
  char *output_buf;
  uint32_t *render_buf;
  size_t render_size;
} tmux_server;

typedef enum {
  TMCD_UNDEF,
  TMCD_SERVER,
  TMCD_CLIENT,
  TMCD_ERROR,
} tmux_command_type;

int tmux_user_out_cmd(const char *input, tmux_command_type *cmd_type);

int run_tmux_mode(Game *g);
