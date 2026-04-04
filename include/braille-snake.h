#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#define BYTES_PER_PIXEL 32
#define TERM_ASPECT_RATIO 2.0

struct term_cell {
  uint32_t symbol;
};

struct snake_ctx {
  size_t nb_cells;
  size_t nb_cols;
  size_t nb_rows;
  size_t output_capacity;
  struct term_cell *front_buffer;
  struct term_cell *back_buffer;
  char *output_buffer;
};

int encode_grid_to_braille(bool in_grid[4][2], unsigned char out_utf8[4]);

void create_buffers(struct snake_ctx *ctx, unsigned int rows,
                    unsigned int cols);

void free_buffers(struct snake_ctx *ctx);

void tau_draw_full(struct snake_ctx *ctx);

void tau_draw_diff(struct snake_ctx *ctx);

void tau_put_str(struct snake_ctx *ctx, char *str, size_t size, int x, int y);
