#include "mode-tty.h"
#include "braille-snake.h"
#include "game.h"
#include <errno.h>
#include <string.h>
#include <sys/poll.h>
#include <termios.h>

struct snake_ctx g_snake_tty_ctx = {0};

char *txt_game_title = "BRAILLE SNAKE";
char *txt_game_over_simple = "[GAMEOVER]";
char *txt_win_simple = "[WIN]";
char *txt_quit_details = "press q/Q/x/X/ESC to quit";
char *txt_reset_details = "press r/R to restart";
char *txt_game_over_one_line = "[GAMEOVER] (q/r?)";
char *txt_win_one_line = "[WIN] (q/r?)";

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

void tty_init(Game *g) {

  struct snake_ctx ctx = {0};
  create_buffers(&ctx, g->total_height, g->total_width);
  g_snake_tty_ctx = ctx;

  if (enable_raw_mode() == -1) {
    perror("enable_raw_mode");
  }
  atexit(restore_terminal);

  draw_first(&ctx);
  draw_full(&ctx);
}

void tty_destroy(Game *g) {

  draw_last(&g_snake_tty_ctx);

  free_buffers(&g_snake_tty_ctx);
}

Command tty_input(char input_char) {
  if (input_char == 'k' || input_char == 'K') {
    return CMD_UP;
  }
  if (input_char == 'j' || input_char == 'J') {
    return CMD_DOWN;
  }
  if (input_char == 'l' || input_char == 'L') {
    return CMD_RIGHT;
  }
  if (input_char == 'h' || input_char == 'H') {
    return CMD_LEFT;
  }
  if (input_char == 'r' || input_char == 'R') {
    return CMD_RESTART;
  }
  if (input_char == 'q' || input_char == 'Q') {
    return CMD_QUIT;
  }
  if (input_char == 'x' || input_char == 'X') {
    return CMD_QUIT;
  }
  return CMD_NONE;
}

void render_game_braille(const Game *g, int offset_x, int offset_y) {

  const Player *player = &g->player;
  int game_width = g->game_width;
  int game_height = g->game_height;

  for (int j = 0, cell_y = 0; j < game_height; j += 4, cell_y++) {
    for (int i = 0, cell_x = 0; i < game_width; i += 2, cell_x++) {
      bool in_grid[4][2] = {0};
      in_grid[0][0] = player->game_array[j * game_width + i];
      in_grid[0][1] = player->game_array[j * game_width + i + 1];
      in_grid[1][0] = player->game_array[(j + 1) * game_width + i];
      in_grid[1][1] = player->game_array[(j + 1) * game_width + (i + 1)];
      in_grid[2][0] = player->game_array[(j + 2) * game_width + i];
      in_grid[2][1] = player->game_array[(j + 2) * game_width + (i + 1)];
      in_grid[3][0] = player->game_array[(j + 3) * game_width + i];
      in_grid[3][1] = player->game_array[(j + 3) * game_width + (i + 1)];
      unsigned char utf8_braille[4];
      uint32_t hexa_braille = 0;
      int rc = encode_grid_to_braille(in_grid, utf8_braille, &hexa_braille);
      size_t pos =
          (size_t)(cell_y + offset_y) * (size_t)g_snake_tty_ctx.nb_cols +
          (size_t)(cell_x + offset_x);
      g_snake_tty_ctx.back_buffer[pos].symbol = hexa_braille;
      if (rc != 0)
        g_snake_tty_ctx.back_buffer[pos].symbol = '!';
    }
  }
}

void draw_edges(const Game *g, unsigned int x, unsigned int y,
                unsigned int width, unsigned int height) {

  bool simple_mode = g->simple_mode;

  uint32_t hline = simple_mode ? '-' : 0x2550;
  uint32_t vline = simple_mode ? '|' : 0x2551;
  uint32_t top_left = simple_mode ? 'x' : 0x2554;
  uint32_t top_right = simple_mode ? 'x' : 0x2557;
  uint32_t bottom_left = simple_mode ? 'x' : 0x255A;
  uint32_t bottom_right = simple_mode ? 'x' : 0x255D;

  for (int i = x; i < x + width; i++) {
    put_utf8(&g_snake_tty_ctx, hline, i, y);
    put_utf8(&g_snake_tty_ctx, hline, i, height + y);
  }
  for (int i = y; i < y + height; i++) {
    put_utf8(&g_snake_tty_ctx, vline, x, i);
    put_utf8(&g_snake_tty_ctx, vline, x + width, i);
  }
  put_utf8(&g_snake_tty_ctx, top_left, x, y);
  put_utf8(&g_snake_tty_ctx, top_right, x + width, y);
  put_utf8(&g_snake_tty_ctx, bottom_left, x, y + height);
  put_utf8(&g_snake_tty_ctx, bottom_right, x + width, y + height);
}

void game_render_tty_running(const Game *g, long long time_frame,
                             double time_now, uint32_t utf8_symbol) {

  const Player *player = &g->player;

  int game_width = g->game_width;
  int total_width = g->total_width;
  int total_height = g->total_height;

  char input_display_content[20];
  char output_display_content[40];
  char output_time_content[40];
  char output_score_content[40];

  clear_everything(&g_snake_tty_ctx);

  if (!g->one_line_mode) {
    int local_room_used = -2;
    put_str(&g_snake_tty_ctx, txt_game_title, strlen(txt_game_title),
            total_width / 2 - (strlen(txt_game_title) / 2), 0);

    snprintf(output_score_content, sizeof(output_score_content), "[score:%3d]",
             player->score);
    local_room_used += strlen(output_score_content);
    if (local_room_used < total_width)
      put_str(&g_snake_tty_ctx, output_score_content,
              strlen(output_score_content),
              total_width - strlen(output_score_content), 1);

    snprintf(output_display_content, sizeof(output_display_content), "[x%d]",
             player->multiplier);
    local_room_used += strlen(output_display_content);
    if (local_room_used < total_width)
      put_str(&g_snake_tty_ctx, output_display_content,
              strlen(output_display_content),
              total_width - (strlen(output_display_content) +
                             strlen(output_score_content)),
              1);

    snprintf(input_display_content, sizeof(input_display_content), "[input: ]");
    local_room_used += strlen(input_display_content);
    if (local_room_used < total_width) {
      put_str(&g_snake_tty_ctx, input_display_content,
              strlen(input_display_content), 0, 1);
      put_utf8(&g_snake_tty_ctx, utf8_symbol, 7, 1);
    }

    draw_edges(g, 0, 2, total_width - g->padding_width + 1,
               total_height - g->padding_height + 1);
    render_game_braille(g, 1, 3);

    local_room_used = -1;
    snprintf(output_display_content, sizeof(output_display_content),
             "[speed:x=%2d,y=%2d]", player->speed_x, player->speed_y);
    local_room_used += strlen(output_display_content);
    if (local_room_used < total_width) {
      put_str(&g_snake_tty_ctx, output_display_content,
              strlen(output_display_content), 0, total_height - 1);
    }
    if (g->god_mode) {
      local_room_used += strlen("[god_mode]");
      if (local_room_used < total_width)
        put_str(&g_snake_tty_ctx, "[god_mode]", strlen(output_display_content),
                strlen(output_display_content), total_height - 1);
    }
    snprintf(output_display_content, sizeof(output_display_content),
             "[time:%4.1f]", time_now);
    local_room_used += strlen(output_display_content);
    if (local_room_used < total_width)
      put_str(&g_snake_tty_ctx, output_display_content,
              strlen(output_display_content),
              total_width - strlen(output_display_content), total_height - 1);

    snprintf(output_time_content, sizeof(output_time_content),
             "[updates:%lldms]", time_frame);
    local_room_used += strlen(output_time_content);
    if (local_room_used < total_width)
      put_str(&g_snake_tty_ctx, output_time_content,
              strlen(output_time_content),
              total_width - strlen(output_display_content) -
                  strlen(output_time_content),
              total_height - 1);
  } else {
    int local_offset = 0;
    if (!g->simple_mode) {
      snprintf(input_display_content, sizeof(input_display_content),
               "[input: ]");
      put_str(&g_snake_tty_ctx, input_display_content,
              strlen(input_display_content), 0, 0);
      local_offset = 9;
      put_utf8(&g_snake_tty_ctx, utf8_symbol, local_offset - 2, 0);
    } else {
      local_offset = 2;
    }
    int game_offset = g->simple_mode ? 0 : local_offset;
    render_game_braille(g, game_offset, 0);
    if (!g->simple_mode) {
      snprintf(output_score_content, sizeof(output_score_content),
               "[score:%3d]", player->score);
      put_str(&g_snake_tty_ctx, output_score_content,
              strlen(output_score_content), game_width / 2 + local_offset, 0);
    }
  }
  draw_diff(&g_snake_tty_ctx);
}

void game_render_tty_dead(const Game *g) {

  int total_width = g->total_width;
  int total_height = g->total_height;

  if (!g->one_line_mode) {
    if (total_width > strlen(txt_quit_details)) {
      put_str(&g_snake_tty_ctx, txt_game_over_simple,
              strlen(txt_game_over_simple),
              total_width / 2 - (strlen(txt_game_over_simple) / 2),
              total_height / 2 - 1);
      put_str(&g_snake_tty_ctx, txt_quit_details, strlen(txt_quit_details),
              total_width / 2 - (strlen(txt_quit_details) / 2),
              total_height / 2);
      put_str(&g_snake_tty_ctx, txt_reset_details, strlen(txt_reset_details),
              total_width / 2 - (strlen(txt_reset_details) / 2),
              total_height / 2 + 4);
    } else {
      put_str(&g_snake_tty_ctx, txt_game_over_one_line,
              strlen(txt_game_over_one_line),
              total_width / 2 - (strlen(txt_game_over_one_line) / 2),
              total_height / 2 - 1);
    }
  } else {
    int offset = total_width;
    if (g->simple_mode) {
      offset = 0;
    }
    put_str(&g_snake_tty_ctx, txt_game_over_one_line,
            strlen(txt_game_over_one_line), offset, 0);
  }
  draw_diff(&g_snake_tty_ctx);
}

void game_render_tty_win(const Game *g) {

  int total_width = g->total_width;
  int total_height = g->total_height;

  if (!g->one_line_mode) {
    if (total_width > strlen(txt_quit_details)) {
      put_str(&g_snake_tty_ctx, txt_win_simple, strlen(txt_win_simple),
              total_width / 2 - (strlen(txt_win_simple) / 2),
              total_height / 2 - 1);
      put_str(&g_snake_tty_ctx, txt_quit_details, strlen(txt_quit_details),
              total_width / 2 - (strlen(txt_quit_details) / 2),
              total_height / 2);
      put_str(&g_snake_tty_ctx, txt_reset_details, strlen(txt_reset_details),
              total_width / 2 - (strlen(txt_reset_details) / 2),
              total_height / 2 + 4);
    } else {
      put_str(&g_snake_tty_ctx, txt_win_one_line, strlen(txt_win_one_line),
              total_width / 2 - (strlen(txt_win_one_line) / 2),
              total_height / 2 - 1);
    }
  } else {
    int offset = total_width;
    if (g->simple_mode) {
      offset = 0;
    }
    put_str(&g_snake_tty_ctx, txt_win_one_line, strlen(txt_win_one_line),
            offset, 0);
  }
  draw_diff(&g_snake_tty_ctx);
}

int run_tty_mode(Game *game) {

  uint32_t utf8_symbol = ' ';

  struct pollfd poll_fd[1];
  poll_fd[0].fd = STDIN_FILENO;
  poll_fd[0].events = POLLIN;
  poll_fd[0].revents = 0;

  long long time_frame = 100;
  long long first_tick = now_ms();
  long long next_tick = first_tick + time_frame;

  GameState game_state = GS_RUN;

  tty_init(game);
  game_render_tty_running(game, time_frame, 0.0, utf8_symbol);

  while (app_is_running()) {
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
      Command command = CMD_NONE;

      if (read(STDIN_FILENO, &c, 1) > 0) {
        if (c == '\x1b') {
          char seq[2];
          if (read(STDIN_FILENO, &seq[0], 1) > 0 &&
              read(STDIN_FILENO, &seq[1], 1) > 0 && seq[0] == '[') {
            switch (seq[1]) {
            case 'A':
              utf8_symbol = 0x2191;
              command = CMD_UP;
              break;
            case 'B':
              utf8_symbol = 0x2193;
              command = CMD_DOWN;
              break;
            case 'C':
              utf8_symbol = 0x2192;
              command = CMD_RIGHT;
              break;
            case 'D':
              utf8_symbol = 0x2190;
              command = CMD_LEFT;
              break;
            }
          } else {
            command = CMD_QUIT;
          }
        } else {
          utf8_symbol = (uint32_t)c;
          command = tty_input(c);
        }
      }

      if (game_state == GS_RUN) {
        if (command != CMD_NONE)
          game_handle_command(game, command);
      } else {
        if (command == CMD_RESTART) {
          game_reset(game);
          first_tick = now_ms();
          next_tick = first_tick + time_frame;
          game_state = GS_RUN;
          game_render_tty_running(game, time_frame, 0.0, utf8_symbol);
        } else if (command == CMD_QUIT) {
          break;
        }
      }

      if (game_state == GS_RUN && (command == CMD_QUIT)) {
        break;
      }
    }

    if (game_state == GS_RUN && now_ms() >= next_tick) {
      game_state = game_tick(game);

      if (game_state == GS_RUN) {
        time_frame = 100 - (game->player.score / 5) * 5;
        if (time_frame < 40)
          time_frame = 40;

        spawn_goal(game);
        double time_now = (now_ms() - first_tick) / 1000.0;
        game_render_tty_running(game, time_frame, time_now, utf8_symbol);
      } else if (game_state == GS_LOSE) {
        game_render_tty_dead(game);
      } else if (game_state == GS_WIN) {
        game_render_tty_win(game);
      }

      next_tick += time_frame;
    }
  }

  tty_destroy(game);
  return EXIT_SUCCESS;
}
