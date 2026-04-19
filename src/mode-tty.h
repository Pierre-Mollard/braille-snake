#pragma once

#include "game.h"

void tty_init(Game *g);

void tty_destroy(Game *g);

Command tty_input(char input_char);

void game_render_tty_running(const Game *g, long long time_frame,
                             double time_now, uint32_t utf8_symbol);
void game_render_tty_dead(const Game *g);
void game_render_tty_win(const Game *g);
