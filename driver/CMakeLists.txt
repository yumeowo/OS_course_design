# 这个CMakeLists.txt仅用于CLion的代码提示和语法检查
# 实际编译请使用 make 命令

cmake_minimum_required(VERSION 3.28.3)

# 确定内核头文件路径的函数
function(find_kernel_headers)
    set(KERNEL_HEADERS_FOUND FALSE PARENT_SCOPE)

    # 优先级1：环境变量
    if(DEFINED ENV{KERNEL_DIR} AND EXISTS $ENV{KERNEL_DIR})
        set(KERNEL_HEADERS $ENV{KERNEL_DIR}/include PARENT_SCOPE)
        set(KERNEL_BASE_DIR $ENV{KERNEL_DIR} PARENT_SCOPE)
        message(STATUS "Using kernel headers from environment: $ENV{KERNEL_DIR}")
        set(KERNEL_HEADERS_FOUND TRUE PARENT_SCOPE)
        return()
    endif()

    # 优先级2：配置文件
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.kernel_path")
        file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../.kernel_path" KERNEL_DIR_FROM_FILE)
        string(STRIP "${KERNEL_DIR_FROM_FILE}" KERNEL_DIR_FROM_FILE)
        # 展开波浪线
        string(REPLACE "~" "$ENV{HOME}" KERNEL_DIR_FROM_FILE "${KERNEL_DIR_FROM_FILE}")

        if(EXISTS "${KERNEL_DIR_FROM_FILE}")
            set(KERNEL_HEADERS ${KERNEL_DIR_FROM_FILE}/include PARENT_SCOPE)
            set(KERNEL_BASE_DIR ${KERNEL_DIR_FROM_FILE} PARENT_SCOPE)
            message(STATUS "Using kernel headers from config file: ${KERNEL_DIR_FROM_FILE}")
            set(KERNEL_HEADERS_FOUND TRUE PARENT_SCOPE)
            return()
        endif()
    endif()

    # 优先级3：默认路径
    execute_process(
            COMMAND uname -r
            OUTPUT_VARIABLE KERNEL_VERSION
            OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    set(DEFAULT_KERNEL_DIR "/lib/modules/${KERNEL_VERSION}/build")
    if(EXISTS ${DEFAULT_KERNEL_DIR})
        set(KERNEL_HEADERS ${DEFAULT_KERNEL_DIR}/include PARENT_SCOPE)
        set(KERNEL_BASE_DIR ${DEFAULT_KERNEL_DIR} PARENT_SCOPE)
        message(STATUS "Using default kernel headers: ${DEFAULT_KERNEL_DIR}")
        set(KERNEL_HEADERS_FOUND TRUE PARENT_SCOPE)
        return()
    endif()

    message(WARNING "Kernel headers not found in any location")
    message(STATUS "Searched locations:")
    message(STATUS "  Environment: $ENV{KERNEL_DIR}")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.kernel_path")
        message(STATUS "  Config file: ${KERNEL_DIR_FROM_FILE}")
    endif()
    message(STATUS "  Default: ${DEFAULT_KERNEL_DIR}")
    message(STATUS "")
    message(STATUS "To specify kernel headers location:")
    message(STATUS "  1. Set environment: export KERNEL_DIR=/path/to/headers")
    message(STATUS "  2. Create config file: echo '/path/to/headers' > ../.kernel_path")
    message(STATUS "  3. Run setup script: ../scripts/setup.sh")
endfunction()

# 检查是否在WSL或Linux环境
if(UNIX)
    find_kernel_headers()

    if(KERNEL_HEADERS_FOUND)
        set(KERNEL_HEADERS_AVAILABLE TRUE)
        message(STATUS "Found kernel headers at: ${KERNEL_HEADERS}")
        message(STATUS "Kernel base directory: ${KERNEL_BASE_DIR}")
    else()
        set(KERNEL_HEADERS_AVAILABLE FALSE)
        message(WARNING "Kernel headers not found. Code completion may be limited.")
    endif()
else()
    message(WARNING "Not on Linux system. Kernel code completion will be limited.")
    set(KERNEL_HEADERS_AVAILABLE FALSE)
endif()

# 创建一个虚拟目标用于IDE代码提示
add_library(myfs_kernel_driver STATIC
        myfs.c
        super.c
        myfs.h
)

# 添加内核相关的宏定义
target_compile_definitions(myfs_kernel_driver PRIVATE
        __KERNEL__
        MODULE
        LINUX
        __linux__
        KBUILD_MODNAME="myfs"
)

# 如果找到内核头文件，添加包含路径
if(KERNEL_HEADERS_AVAILABLE)
    target_include_directories(myfs_kernel_driver PRIVATE
            ${KERNEL_HEADERS}
            ${KERNEL_HEADERS}/linux
            ${KERNEL_HEADERS}/asm
            ${KERNEL_HEADERS}/uapi
            ${KERNEL_BASE_DIR}/arch/x86/include
            ${KERNEL_BASE_DIR}/arch/x86/include/generated
            ${KERNEL_BASE_DIR}/include/generated
    )
endif()

# 设置编译选项（模拟内核编译环境）
target_compile_options(myfs_kernel_driver PRIVATE
        -Wall
        -Wundef
        -Wstrict-prototypes
        -Wno-trigraphs
        -fno-strict-aliasing
        -fno-common
        -Wno-format-security
        -fno-delete-null-pointer-checks
        -std=gnu11
        -fno-reorder-blocks
        -nostdinc
)

# 为了在IDE中能看到所有文件，添加头文件
target_sources(myfs_kernel_driver PRIVATE myfs.h)

# 设置目标属性
set_target_properties(myfs_kernel_driver PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED ON
)

# 添加自定义目标用于实际编译
if(KERNEL_HEADERS_AVAILABLE)
    add_custom_target(build_kernel_driver
            COMMAND $(MAKE) -C . all KERNEL_DIR=${KERNEL_BASE_DIR}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Building kernel driver using Makefile with kernel dir: ${KERNEL_BASE_DIR}"
            VERBATIM
    )
else()
    add_custom_target(build_kernel_driver
            COMMAND $(MAKE) -C . all
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            COMMENT "Building kernel driver using Makefile (may fail without kernel headers)"
            VERBATIM
    )
endif()

add_custom_target(clean_kernel_driver
        COMMAND $(MAKE) -C . clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Cleaning kernel driver"
        VERBATIM
)

# 显示编译说明
message(STATUS "=== MyFS Kernel Driver ===")
if(KERNEL_HEADERS_AVAILABLE)
    message(STATUS "Kernel headers: ${KERNEL_BASE_DIR}")
    message(STATUS "For actual compilation, use:")
    message(STATUS "  cd ${CMAKE_CURRENT_SOURCE_DIR}")
    message(STATUS "  make KERNEL_DIR=${KERNEL_BASE_DIR}")
else()
    message(STATUS "For actual compilation, use:")
    message(STATUS "  cd ${CMAKE_CURRENT_SOURCE_DIR}")
    message(STATUS "  make KERNEL_DIR=/path/to/kernel/headers")
    message(STATUS "Or configure kernel headers first:")
    message(STATUS "  ../scripts/setup.sh")
endif()
message(STATUS "To load the module:")
message(STATUS "  make load")
message(STATUS "To unload the module:")
message(STATUS "  make unload")