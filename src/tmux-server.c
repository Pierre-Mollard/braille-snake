#include "braille-snake.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

static void print_scene(void) { printf("XXXXXX"); }

static int run_server(void) {
  int lock_fd = -1;
  int server_fd = -1;
  struct sockaddr_un addr;

  lock_fd = open(TMUX_LOCK_FILE, O_RDWR | O_CREAT, 0600);
  if (lock_fd == -1) {
    perror("open lock file");
    return EXIT_FAILURE;
  }

  if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) {
    fprintf(stderr, "braille-snake: server already running\n");
    // TODO: return pid of server running
    close(lock_fd);
    return EXIT_SUCCESS;
  }

  server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (server_fd == -1) {
    perror("socket");
    close(lock_fd);
    return EXIT_FAILURE;
  }

  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, TMUX_SOCK_FILE, sizeof(addr.sun_path) - 1);

  unlink(TMUX_SOCK_FILE);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
    perror("bind");
    close(server_fd);
    close(lock_fd);
    return EXIT_FAILURE;
  }

  if (listen(server_fd, 8) == -1) {
    perror("listen");
    unlink(TMUX_SOCK_FILE);
    close(server_fd);
    close(lock_fd);
    return EXIT_FAILURE;
  }

  while (true) {
    int client_fd = accept(server_fd, NULL, NULL);
    if (client_fd == -1) {
      perror("accept");
      break;
    }

    /* read client command, update state, maybe write response */

    char buffer[512];

    // read while 0 char read
    printf("reading...\n");
    size_t n = read(client_fd, buffer, sizeof(buffer));
    if (n < 0) {
      close(client_fd);
      continue;
    }

    // TODO: move main to gameloop and multi thread

    buffer[n] = '\0';
    printf("read: %s\n", buffer);

    close(client_fd);
  }

  unlink(TMUX_SOCK_FILE);
  close(server_fd);
  close(lock_fd);
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

int tmux_server_mode(const char *input, bool *load_game) {
  *load_game = false;
  if (strcmp(input, "init") == 0) {
    printf("init mode\n");
    int is_running = run_server();
    if (is_running == EXIT_SUCCESS)
      *load_game = true;
    return is_running;
  } else if (strcmp(input, "render") == 0) {
    print_scene();
    return EXIT_SUCCESS;
    // TODO: use server data to print, return send_data_unix(input);
  } else if (strcmp(input, "up") == 0) {
    return send_data_unix(input);
  } else if (strcmp(input, "down") == 0) {
    return send_data_unix(input);
  } else if (strcmp(input, "right") == 0) {
    return send_data_unix(input);
  } else if (strcmp(input, "left") == 0) {
    return send_data_unix(input);
  }

  return EXIT_FAILURE;
}
