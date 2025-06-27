cmake_minimum_required(VERSION 3.28.3)
project(MySimpleFS C)

set(CMAKE_C_STANDARD 11)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 用户空间工具和测试程序
add_subdirectory(tools)

# 添加主程序（如果是用户空间程序）
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/main.c")
    add_executable(myfs_main main.c)
    target_link_libraries(myfs_main)
endif()

# 内核驱动部分不使用CMake编译
# 驱动模块使用Linux内核构建系统（Makefile）
message(STATUS "Kernel driver should be built using: cd driver && make")
message(STATUS "User-space tools will be built with CMake")