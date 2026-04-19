cmake_minimum_required(VERSION 3.28.3)

project(BrailleSnake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(BrailleSnake)
target_compile_options(BrailleSnake PRIVATE -Wall)
target_link_libraries(BrailleSnake PRIVATE m)
target_sources(BrailleSnake
  PRIVATE
    src/main.c
    src/char-encoder.c
    src/double-buffer.c
    src/tmux-server.c
    src/game.h
    src/game.c
    src/mode-tmux.c
    src/mode-tmux.h
    src/mode-tty.c
    src/mode-tty.h

  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS
      include
    FILES
      include/braille-snake.h
)

add_executable(BrailleSnakeMain)
target_sources(BrailleSnakeMain PRIVATE src/main.c)
target_link_libraries(BrailleSnakeMain PRIVATE BrailleSnake)

add_executable(TestCharList)
target_sources(TestCharList PRIVATE tests/char_list.c)
target_link_libraries(TestCharList PRIVATE BrailleSnake)
