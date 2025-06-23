cmake_minimum_required(VERSION 3.28.3)
project(driver)

set(CMAKE_C_STANDARD 11)

# 添加 driver 目录下的源文件
file(GLOB DRIVER_SOURCES "*.c")

add_executable(driver_main ${DRIVER_SOURCES})
# 或者如果是库：
# add_library(driver_lib ${DRIVER_SOURCES})
