#pragma once

#include "game.h"

typedef enum {
  TMCD_UNDEF,
  TMCD_SERVER,
  TMCD_CLIENT,
  TMCD_ERROR,
} tmux_command_type;

int tmux_handle_command(const char *input, tmux_command_type *cmd_type);
