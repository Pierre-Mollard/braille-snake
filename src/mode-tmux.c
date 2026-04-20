#include "mode-tmux.h"
#include "braille-snake.h"
#include "game.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static int server_init(tmux_server *srv, Game *g) {
  struct sockaddr_un addr;

  srv->fd_lock = open(TMUX_LOCK_FILE, O_RDWR | O_CREAT, 0600);
  if (srv->fd_lock == -1) {
    perror("open lock file");
    return EXIT_FAILURE;
  }

  if (flock(srv->fd_lock, LOCK_EX | LOCK_NB) == -1) {
    char pidbuf[32] = {0};
    lseek(srv->fd_lock, 0, SEEK_SET);
    ssize_t n = read(srv->fd_lock, pidbuf, sizeof(pidbuf) - 1);
    if (n > 0) {
      fprintf(stderr, "braille-snake: server already running (pid %s)\n",
              pidbuf);
    } else {
      fprintf(stderr, "braille-snake: server already running\n");
    }
    close(srv->fd_lock);
    return EXIT_FAILURE;
  }

  ftruncate(srv->fd_lock, 0);
  dprintf(srv->fd_lock, "%ld", (long)getpid());
  fsync(srv->fd_lock);

  srv->fd_sock = socket(AF_UNIX, SOCK_STREAM, 0);
  if (srv->fd_sock == -1) {
    perror("socket");
    close(srv->fd_lock);
    return EXIT_FAILURE;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, TMUX_SOCK_FILE, sizeof(addr.sun_path) - 1);

  srv->render_size = g->game_width;
  srv->render_buf = calloc(srv->render_size, sizeof(*srv->render_buf));
  srv->output_buf =
      calloc(srv->render_size * UTF8_RES, sizeof(*srv->output_buf));

  unlink(TMUX_SOCK_FILE);

  if (bind(srv->fd_sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    close(srv->fd_sock);
    close(srv->fd_lock);
    free(srv->render_buf);
    free(srv->output_buf);
    return EXIT_FAILURE;
  }

  if (listen(srv->fd_sock, 8) == -1) {
    perror("listen");
    unlink(TMUX_SOCK_FILE);
    close(srv->fd_sock);
    close(srv->fd_lock);
    free(srv->render_buf);
    free(srv->output_buf);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

static int server_shutdown(tmux_server *srv) {
  unlink(TMUX_SOCK_FILE);
  close(srv->fd_sock);
  close(srv->fd_lock);
  free(srv->render_buf);
  free(srv->output_buf);
  return EXIT_SUCCESS;
}

static int send_data_unix(const char *content) {

  int client_fd = -1;
  struct sockaddr_un addr;

  client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (client_fd == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, TMUX_SOCK_FILE, sizeof(addr.sun_path) - 1);

  if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("connect");
    close(client_fd);
    return EXIT_FAILURE;
  }

  if (write(client_fd, content, strlen(content)) == -1) {
    perror("write");
    close(client_fd);
    return EXIT_FAILURE;
  }

  close(client_fd);
  return EXIT_SUCCESS;
}

void render_game_braille_tmux(const Game *g, tmux_server *srv) {

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
      size_t pos = (size_t)(cell_y)*srv->render_size + (size_t)(cell_x);
      srv->render_buf[pos] = hexa_braille;
      if (rc != 0)
        srv->render_buf[pos] = '!';
    }
  }
}

// NOTE: this could be optimized by separating 'render' (render_utf8_tmux) and
// 'process' (render_game_braille_tmux)
void render_utf8_tmux(tmux_server *srv) {
  char *cursor = srv->output_buf;
  for (size_t i = 0; i < srv->render_size; i++) {

    uint32_t hex = srv->render_buf[i];
    char utf8[4];
    size_t symbol_len = utf8_encode(hex, utf8);
    for (size_t i = 0; i < symbol_len; i++) {
      *cursor++ = utf8[i];
    }
  }
}

static int receive_data_unix(const char *content, size_t max_size,
                             char *response, size_t *size) {

  int client_fd = -1;
  struct sockaddr_un addr;

  client_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (client_fd == -1) {
    perror("socket");
    return EXIT_FAILURE;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, TMUX_SOCK_FILE, sizeof(addr.sun_path) - 1);

  if (connect(client_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("connect");
    close(client_fd);
    return EXIT_FAILURE;
  }

  if (write(client_fd, content, strlen(content)) == -1) {
    perror("write");
    close(client_fd);
    return EXIT_FAILURE;
  }

  size_t n = read(client_fd, response, max_size);
  if (n < 0) {
    close(client_fd);
    return EXIT_FAILURE;
  }

  response[n] = '\0';
  *size = n;

  close(client_fd);
  return EXIT_SUCCESS;
}

Command server_handle_ready_client(tmux_server *srv, Game *game,
                                   GameState state) {

  int client_fd = accept(srv->fd_sock, NULL, NULL);
  if (client_fd == -1) {
    perror("accept");
    return CMD_NONE;
  }

  char buffer[512];
  size_t n = read(client_fd, buffer, sizeof(buffer));
  if (n < 0) {
    close(client_fd);
    return CMD_NONE;
  }

  buffer[n] = '\0';
  printf("read: %s\n", buffer);

  if (strcmp(buffer, "render") == 0) {

    switch (state) {
    case GS_RUN:
      render_game_braille_tmux(game, srv);
      render_utf8_tmux(srv);
      if (write(client_fd, srv->output_buf, srv->render_size * UTF8_RES) ==
          -1) {
        perror("write");
        close(client_fd);
        return CMD_NONE;
      }
      break;
    case GS_LOSE:
      if (write(client_fd, "GAMEOVER", 8) == -1) {
        perror("write");
        close(client_fd);
        return CMD_NONE;
      }
      break;
    case GS_WIN:
      if (write(client_fd, "WIN", 3) == -1) {
        perror("write");
        close(client_fd);
        return CMD_NONE;
      }
      break;
    }
  } else if (strcmp(buffer, "up") == 0) {
    return CMD_UP;
  } else if (strcmp(buffer, "down") == 0) {
    return CMD_DOWN;
  } else if (strcmp(buffer, "right") == 0) {
    return CMD_RIGHT;
  } else if (strcmp(buffer, "left") == 0) {
    return CMD_LEFT;
  } else if (strcmp(buffer, "restart") == 0) {
    return CMD_RESTART;
  } else if (strcmp(buffer, "quit") == 0) {
    return CMD_QUIT;
  }

  close(client_fd);
  return CMD_NONE;
}

// NOTE: client set a max buffer size because it doesnt know the actual size
// from server, this could be store in file but would reduce perfs
int tmux_user_out_cmd(const char *input, tmux_command_type *cmd_type) {
  if (strcmp(input, "init") == 0) {
    *cmd_type = TMCD_SERVER;
    return EXIT_SUCCESS;
  } else if (strcmp(input, "render") == 0) {
    *cmd_type = TMCD_CLIENT;
    char buffer[1024];
    size_t size;
    int rc = receive_data_unix(input, 1024, buffer, &size);
    buffer[size] = '\0';
    printf("%s", buffer);
    return rc;
  } else if (strcmp(input, "up") == 0) {
    *cmd_type = TMCD_CLIENT;
    return send_data_unix(input);
  } else if (strcmp(input, "down") == 0) {
    *cmd_type = TMCD_CLIENT;
    return send_data_unix(input);
  } else if (strcmp(input, "right") == 0) {
    *cmd_type = TMCD_CLIENT;
    return send_data_unix(input);
  } else if (strcmp(input, "left") == 0) {
    *cmd_type = TMCD_CLIENT;
    return send_data_unix(input);
  } else if (strcmp(input, "restart") == 0) {
    *cmd_type = TMCD_CLIENT;
    return send_data_unix(input);
  } else if (strcmp(input, "quit") == 0) {
    *cmd_type = TMCD_CLIENT;
    return send_data_unix(input);
  }

  *cmd_type = TMCD_UNDEF;
  return EXIT_FAILURE;
}

int run_tmux_mode(Game *g) {
  tmux_server srv;
  if (server_init(&srv, g) != 0)
    return EXIT_FAILURE;

  long long time_frame = 100;
  long long first_tick = now_ms();
  long long next_tick = first_tick + time_frame;
  GameState state = GS_RUN;
  Command command = CMD_NONE;

  printf("running server mode\n");
  while (app_is_running()) {
    long long ms_left = next_tick - now_ms();
    if (ms_left < 0)
      ms_left = 0;

    struct pollfd pfd = {.fd = srv.fd_sock, .events = POLLIN, .revents = 0};

    int ret = poll(&pfd, 1, (int)ms_left);
    if (ret == -1) {
      if (errno == EINTR)
        continue;
      break;
    }

    if (ret > 0 && (pfd.revents & POLLIN)) {
      command = server_handle_ready_client(&srv, g, state);

      if (state == GS_RUN) {
        if (command != CMD_NONE)
          game_handle_command(g, command);
      } else {
        if (command == CMD_RESTART) {
          printf("reset after end\n");
          game_reset(g);
          first_tick = now_ms();
          next_tick = first_tick + time_frame;
          state = GS_RUN;
        } else if (command == CMD_QUIT) {
          printf("quit after end\n");
          break;
        }
      }

      if (state == GS_RUN && (command == CMD_QUIT)) {
        printf("quit during game\n");
        break;
      }
      if (state == GS_RUN && (command == CMD_RESTART)) {
        printf("restart during game\n");
        game_reset(g);
        first_tick = now_ms();
        next_tick = first_tick + time_frame;
        state = GS_RUN;
      }
    }

    if (state == GS_RUN && now_ms() >= next_tick) {
      state = game_tick(g);

      time_frame = 100 - (g->player.score / 5) * 5;
      if (time_frame < 40)
        time_frame = 40;

      spawn_goal(g);

      next_tick += time_frame;
    }
  }

  server_shutdown(&srv);
  return EXIT_SUCCESS;
}
