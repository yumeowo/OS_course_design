cmake_minimum_required(VERSION 3.28.3)
project(tools)

set(CMAKE_C_STANDARD 11)

# 添加 tools 目录下的源文件
file(GLOB TOOLS_SOURCES "*.c")

add_executable(tools_main ${TOOLS_SOURCES})
# 或者如果是库：
# add_library(tools_lib ${TOOLS_SOURCES})

