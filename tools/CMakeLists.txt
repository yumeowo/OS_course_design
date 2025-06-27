cmake_minimum_required(VERSION 3.28.3)

# 设置C标准
set(CMAKE_C_STANDARD 11)

# 创建mkfs工具
add_executable(mkfs mkfs.c)

# 设置编译选项
target_compile_options(mkfs PRIVATE -Wall -Wextra -std=c11)

# 如果有其他工具源文件，添加相应的可执行文件
file(GLOB OTHER_TOOLS "*.c")
foreach(TOOL_SOURCE ${OTHER_TOOLS})
    get_filename_component(TOOL_NAME ${TOOL_SOURCE} NAME_WE)
    if(NOT ${TOOL_NAME} STREQUAL "mkfs")
        add_executable(${TOOL_NAME} ${TOOL_SOURCE})
        target_compile_options(${TOOL_NAME} PRIVATE -Wall -Wextra -std=c11)
    endif()
endforeach()

message(STATUS "User-space tools configured for compilation")