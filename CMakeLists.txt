cmake_minimum_required(VERSION 3.24)
project(SP C)

set(CMAKE_C_STANDARD 23)

add_executable(SP HW1/main.c HW1/libcoro.c)
add_executable(HW2 HW2/main.c)
add_executable(HW3 HW3/main.c HW3/userfs.c)
add_executable(test HW3/test.c HW3/userfs.c)
