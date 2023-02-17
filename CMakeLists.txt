cmake_minimum_required(VERSION 3.24)
project(SP C)

set(CMAKE_C_STANDARD 23)

add_executable(SP HW1/main.c HW1/libcoro.c)
