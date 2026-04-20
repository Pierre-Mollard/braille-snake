#include "mode-tmux.h"
#include "braille-snake.h"
#include "game.h"
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static void print_scene(void) { printf("XXXXXX"); }

static int server_init(tmux_server *srv) {
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

  unlink(TMUX_SOCK_FILE);

  if (bind(srv->fd_sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    close(srv->fd_sock);
    close(srv->fd_lock);
    return EXIT_FAILURE;
  }

  if (listen(srv->fd_sock, 8) == -1) {
    perror("listen");
    unlink(TMUX_SOCK_FILE);
    close(srv->fd_sock);
    close(srv->fd_lock);
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

static int server_shutdown(tmux_server *srv) {
  unlink(TMUX_SOCK_FILE);
  close(srv->fd_sock);
  close(srv->fd_lock);
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

void server_handle_ready_client(tmux_server *srv, Game *game) {

  int client_fd = accept(srv->fd_sock, NULL, NULL);
  if (client_fd == -1) {
    perror("accept");
    return;
  }

  /* read client command, update state, maybe write response */
  char buffer[512];

  // read while 0 char read
  printf("reading...\n");
  size_t n = read(client_fd, buffer, sizeof(buffer));
  if (n < 0) {
    close(client_fd);
    return;
  }

  buffer[n] = '\0';
  printf("read: %s\n", buffer);

  close(client_fd);
}

int tmux_handle_command(const char *input, tmux_command_type *cmd_type) {
  if (strcmp(input, "init") == 0) {
    *cmd_type = TMCD_SERVER;
    return EXIT_SUCCESS;
  } else if (strcmp(input, "render") == 0) {
    *cmd_type = TMCD_CLIENT;
    print_scene();
    // TODO: use server data to print, return send_data_unix(input);
    return EXIT_SUCCESS;
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
  }

  *cmd_type = TMCD_UNDEF;
  return EXIT_FAILURE;
}

int run_tmux_mode(Game *g) {
  tmux_server srv;
  if (server_init(&srv) != 0)
    return EXIT_FAILURE;

  long long time_frame = 100;
  long long next_tick = now_ms() + time_frame;
  GameState state = GS_RUN;

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
      server_handle_ready_client(&srv, g);
    }

    if (state == GS_RUN && now_ms() >= next_tick) {
      state = game_tick(g);
      next_tick += time_frame;
    }
  }

  server_shutdown(&srv);
  return EXIT_SUCCESS;
}
