#include "braille-snake.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void create_buffers(struct snake_ctx *ctx, unsigned int rows,
                    unsigned int cols) {
  ctx->nb_rows = rows;
  ctx->nb_cols = cols;
  ctx->nb_cells = ctx->nb_rows * ctx->nb_cols;
  ctx->back_buffer = calloc(ctx->nb_cells, sizeof(*ctx->back_buffer));
  ctx->front_buffer = calloc(ctx->nb_cells, sizeof(*ctx->front_buffer));

  ctx->output_capacity = ctx->nb_cells * BYTES_PER_PIXEL;
  ctx->output_buffer =
      calloc(ctx->output_capacity, sizeof(*ctx->output_buffer));

  if (!ctx->back_buffer || !ctx->front_buffer || !ctx->output_buffer) {
    free(ctx->back_buffer);
    free(ctx->front_buffer);
    free(ctx->output_buffer);
    free(ctx);
    perror("calloc error");
  }
}

void free_buffers(struct snake_ctx *ctx) {
  free(ctx->back_buffer);
  free(ctx->front_buffer);
  free(ctx->output_buffer);
  ctx->back_buffer = NULL;
  ctx->front_buffer = NULL;
  ctx->output_buffer = NULL;
}

static inline void write_in_term(const char *seq) {
  write(STDOUT_FILENO, seq, strlen(seq));
}

static inline void write_in_buffer(char **buffer_cursor, const char *seq) {
  size_t len = strlen(seq);
  memcpy(*buffer_cursor, seq, len);
  *buffer_cursor += len;
  **buffer_cursor = '\0';
}

static inline void write_in_buffer_f(char **buffer_cursor, const char *format,
                                     ...) {

  va_list args;
  va_start(args, format);

  int len = vsprintf(*buffer_cursor, format, args);

  va_end(args);

  *buffer_cursor += len;
}

static inline void write_in_buffer_move(char **buffer_cursor, int row,
                                        int col) {
  write_in_buffer_f(buffer_cursor, CURSOR_TO_F, row, col);
}

int cells_differ(struct term_cell a, struct term_cell b) {
  return a.symbol != b.symbol;
}

void draw_first(struct snake_ctx *ctx) {
  write_in_term(ALTERNATIVE_BUFFER_ON);
  write_in_term(HIDE_CURSOR);
}

void draw_last(struct snake_ctx *ctx) {
  write_in_term(ALTERNATIVE_BUFFER_OFF);
  write_in_term(SHOW_CURSOR);
  write_in_term(CLEAR_ALL);
}

void draw_full(struct snake_ctx *ctx) {
  if (!ctx || !ctx->back_buffer || !ctx->output_buffer)
    return;

  char *cursor = ctx->output_buffer;

  write_in_buffer(&cursor, CURSOR_HOME);

  // COPY back buffer to output
  for (size_t y = 0; y < ctx->nb_rows; y++) {
    for (size_t x = 0; x < ctx->nb_cols; x++) {
      size_t i = y * ctx->nb_cols + x;
      struct term_cell cell = ctx->back_buffer[i];

      char utf8[4];
      size_t symbol_len = utf8_encode(cell.symbol, utf8);
      for (size_t i = 0; i < symbol_len; i++) {
        *cursor++ = utf8[i];
      }
    }

    if (y + 1 < ctx->nb_rows) {
      *cursor++ = '\r';
      *cursor++ = '\n';
    }
  }

  // DRAWS once ouput
  size_t len = (size_t)(cursor - ctx->output_buffer);
  write(STDOUT_FILENO, ctx->output_buffer, len);

  // UPDATES front buffer to be what is on screen
  memcpy(ctx->front_buffer, ctx->back_buffer,
         ctx->nb_cells * sizeof(*ctx->front_buffer));
}

// draws the difference between back buffer (what is new from the user)
// and front buffer (believed to be on screen)
// NOTE: current algo redraws in-between first and last diff in each row
void draw_diff(struct snake_ctx *ctx) {
  if (!ctx || !ctx->front_buffer || !ctx->back_buffer || !ctx->output_buffer)
    return;

  char *cursor = ctx->output_buffer;

  for (size_t row = 0; row < ctx->nb_rows; row++) {
    int diff_in_line = 0;
    int first_index = 0;
    int last_index = 0;
    int first_diff_x = 0;

    for (size_t col = 0; col < ctx->nb_cols; col++) {
      size_t i = row * ctx->nb_cols + col;

      if (cells_differ(ctx->back_buffer[i], ctx->front_buffer[i])) {
        // first diff in line, get the x value
        if (!diff_in_line) {
          diff_in_line = 1;
          first_diff_x = col;
          first_index = i;
        }
        // last diff in line is always the latest
        last_index = i;
      }
    }

    if (diff_in_line) {
      write_in_buffer_move(&cursor, row + 1, first_diff_x + 1);

      for (size_t i = first_index; i <= last_index; i++) {
        struct term_cell cell = ctx->back_buffer[i];

        char utf8[4];
        size_t symbol_len = utf8_encode(cell.symbol, utf8);
        for (size_t i = 0; i < symbol_len; i++) {
          *cursor++ = utf8[i];
        }
      }

      // UPDATES front buffer to be what is on screen (with only the part that
      // changed)
      memcpy(&ctx->front_buffer[first_index], &ctx->back_buffer[first_index],
             (last_index - first_index + 1) * sizeof(*ctx->back_buffer));
    }
  }

  // DRAWS differences found
  size_t len = (size_t)(cursor - ctx->output_buffer);
  if (len > 0) {
    write(STDOUT_FILENO, ctx->output_buffer, len);
  }
}

void put_str(struct snake_ctx *ctx, char *str, size_t size, int x, int y) {
  if (!ctx || !str)
    return;

  int screen_width = (int)ctx->nb_cols;
  int screen_height = (int)ctx->nb_rows;

  if (y < 0 || y >= screen_height)
    return;

  if (x >= screen_width)
    return;

  int start_x = (x < 0) ? 0 : x;
  if (x < 0)
    start_x = 0;

  for (size_t i = 0; i < size; i++) {
    if (start_x + (int)i >= screen_width)
      break;
    if (str[i] == '\0')
      break;
    size_t pos = (ctx->nb_cols * (size_t)y) + (size_t)start_x + i;
    ctx->back_buffer[pos].symbol = str[i];
  }
}

void put_utf8(struct snake_ctx *ctx, uint32_t hex, int x, int y) {
  if (x < 0 || y < 0 || x >= (int)ctx->nb_cols || y >= (int)ctx->nb_rows) {
    fprintf(stderr, "put_utf8 OOB: x=%d y=%d cols=%zu rows=%zu hex=%u\n", x, y,
            ctx->nb_cols, ctx->nb_rows, hex);
    abort();
  }
  size_t pos = (ctx->nb_cols * (size_t)y) + (size_t)x;
  ctx->back_buffer[pos].symbol = hex;
}

void clear_everything(struct snake_ctx *ctx) {
  for (int i = 0; i < ctx->nb_cells; i++) {
    ctx->back_buffer[i].symbol = ' ';
  }
}
