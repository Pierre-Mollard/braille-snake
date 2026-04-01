cmake_minimum_required(VERSION 3.28.3)

project(BrailleSnake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(BrailleSnakeMain)
target_compile_options(BrailleSnakeMain PRIVATE -Wall)
target_link_libraries(BrailleSnakeMain PRIVATE m)
target_sources(BrailleSnakeMain
  PRIVATE
    src/main.c
)
