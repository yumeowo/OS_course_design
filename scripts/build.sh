#!/bin/bash

# MyFS构建脚本 - 专为WSL环境设计

set -e  # 出错时退出

echo "=== MyFS构建脚本 (WSL) ==="

# 检查WSL环境
check_wsl_environment() {
    if [[ ! -f /proc/version ]] || ! grep -qi "microsoft\|wsl" /proc/version; then
        echo "错误：此脚本仅适用于WSL环境"
        echo "请在Windows Subsystem for Linux中运行此脚本"
        exit 1
    fi

    echo "✓ 检测到WSL环境"
    echo "内核版本: $(uname -r)"
}

# 确定内核头文件路径
determine_kernel_dir() {
    local kernel_dir=""

    # 优先级1：命令行环境变量
    if [ -n "$KERNEL_DIR" ]; then
        kernel_dir="$KERNEL_DIR"
    # 优先级2：配置文件
    elif [ -f ".kernel_path" ]; then
        kernel_dir=$(cat .kernel_path)
    # 优先级3：默认路径
    else
        kernel_dir="/lib/modules/$(uname -r)/build"
    fi

    # 展开波浪线
    kernel_dir="${kernel_dir/#\~/$HOME}"

    # 导出给Makefile使用
    export KERNEL_DIR="$kernel_dir"

    echo "$kernel_dir"
}

# 检查内核头文件
check_kernel_headers() {
    local kernel_dir=$(determine_kernel_dir)

    if [ ! -d "$kernel_dir" ]; then
        echo ""
        echo "⚠ 错误：内核头文件路径不存在: $kernel_dir"
        echo ""
        echo "请通过以下方式之一指定正确的内核头文件路径："
        echo ""
        echo "1. 设置环境变量："
        echo "   export KERNEL_DIR=/path/to/kernel/headers"
        echo "   ./scripts/build.sh"
        echo ""
        echo "2. 临时指定："
        echo "   KERNEL_DIR=/path/to/kernel/headers ./scripts/build.sh"
        echo ""
        echo "3. 创建配置文件："
        echo "   echo '/path/to/kernel/headers' > .kernel_path"
        echo "   ./scripts/build.sh"
        echo ""
        echo "4. 重新运行setup.sh配置："
        echo "   ./scripts/setup.sh"
        echo ""
        read -p "是否仍要继续构建用户空间工具？(y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "构建已取消"
            exit 1
        fi
        return 1
    fi

    echo "✓ 找到内核头文件: $kernel_dir"

    # 验证关键头文件是否存在
    if [ -f "$kernel_dir/include/linux/module.h" ] || [ -f "$kernel_dir/include/linux/kernel.h" ]; then
        echo "✓ 内核头文件验证通过"
    else
        echo "⚠ 警告：内核头文件可能不完整，编译可能失败"
        echo "请确保内核头文件完整安装"
    fi

    return 0
}

# 构建用户空间工具
build_userspace_tools() {
    echo ""
    echo "=== 构建用户空间工具 ==="

    # 清理并创建构建目录
    rm -rf build
    mkdir -p build
    cd build

    # 运行CMake配置
    echo "运行CMake配置..."
    cmake ..

    # 编译
    echo "编译用户空间工具..."
    make -j$(nproc)

    cd ..
    echo "✓ 用户空间工具构建完成"

    # 显示构建结果
    if [ -d "build/bin" ]; then
        echo "生成的工具："
        ls -la build/bin/
    fi
}

# 构建内核驱动
build_kernel_driver() {
    echo ""
    echo "=== 构建内核驱动 ==="

    cd driver

    # 清理之前的构建
    make clean 2>/dev/null || true

    # 显示使用的内核路径
    echo "使用内核路径: $KERNEL_DIR"

#    # 执行WSL特定的内核准备步骤
#    echo "准备内核构建环境..."
#    if [ -f "${KERNEL_DIR}/Microsoft/config-wsl" ]; then
#        echo "✓ 找到WSL配置文件"
#        cp "${KERNEL_DIR}/Microsoft/config-wsl" "${KERNEL_DIR}/.config"
#
#        # 执行make prepare
#        echo "执行 make prepare..."
#        (cd "${KERNEL_DIR}" && make -j$(nproc) prepare)
#
#        # 执行make modules_prepare
#        echo "执行 make modules_prepare..."
#        (cd "${KERNEL_DIR}" && make -j$(nproc) modules_prepare)
#    else
#        echo "⚠ 警告：未找到WSL配置文件 (${KERNEL_DIR}/Microsoft/config-wsl)"
#        echo "将尝试直接构建模块，但可能会失败"
#    fi

    # 编译内核模块
    echo "编译内核驱动模块..."
    if make KERNEL_DIR="$KERNEL_DIR"; then
        echo "✓ 内核驱动构建完成"

        # 显示构建结果
        if [ -f "myfs.ko" ]; then
            echo "生成的内核模块："
            ls -la myfs.ko
            echo "模块信息："
            modinfo myfs.ko 2>/dev/null || echo "无法获取模块信息"
        fi
    else
        echo "✗ 内核驱动构建失败"
        echo "请检查内核头文件路径是否正确: $KERNEL_DIR"
        echo "或查看上面的错误信息进行诊断"
        cd ..
        return 1
    fi

    cd ..
}

# 显示使用说明
show_usage_instructions() {
    echo ""
    echo "=== 构建完成 ==="
    echo ""
    echo "生成的文件："
    echo "- 用户空间工具: build/bin/"
    echo "- 内核驱动模块: driver/myfs.ko"
    echo ""
    echo "使用流程："
    echo ""
    echo "1. 加载内核驱动："
    echo "   cd driver"
    echo "   sudo make load"
    echo "   # 或指定内核路径："
    echo "   sudo make load KERNEL_DIR=\"$KERNEL_DIR\""
    echo ""
    echo "2. 创建文件系统镜像："
    echo "   dd if=/dev/zero of=myfs.img bs=1M count=100"
    echo "   ./build/bin/mkfs myfs.img"
    echo ""
    echo "3. 挂载文件系统："
    echo "   sudo mkdir -p /mnt/myfs"
    echo "   sudo ./scripts/mount.sh myfs.img /mnt/myfs"
    echo ""
    echo "4. 卸载文件系统："
    echo "   sudo ./scripts/unmount.sh /mnt/myfs"
    echo ""
    echo "5. 卸载内核驱动："
    echo "   cd driver"
    echo "   sudo make unload"
    echo ""
    echo "内核路径配置："
    if [ -n "$KERNEL_DIR" ]; then
        echo "- 当前使用: $KERNEL_DIR"
        if [ -f ".kernel_path" ]; then
            echo "- 配置文件: .kernel_path"
        fi
        if [ -n "$KERNEL_DIR" ] && [ "$KERNEL_DIR" != "/lib/modules/$(uname -r)/build" ]; then
            echo "- 环境变量: KERNEL_DIR"
        fi
    fi
    echo ""
    echo "注意事项："
    echo "- 所有内核模块操作都需要sudo权限"
    echo "- 挂载前确保目标目录存在且为空"
    echo "- 卸载前确保没有进程正在使用文件系统"
    echo "- 使用 'dmesg | tail' 查看内核日志"
    echo "- 如需更改内核路径，可重新运行 ./scripts/setup.sh"
}

# 主函数
main() {
    local kernel_headers_available=false

    check_wsl_environment

    if check_kernel_headers; then
        kernel_headers_available=true
    fi

    # 构建用户空间工具
    build_userspace_tools

    # 只有在内核头文件可用时才构建内核驱动
    if [ "$kernel_headers_available" = true ]; then
        build_kernel_driver
    else
        echo ""
        echo "⚠ 跳过内核驱动构建（缺少内核头文件）"
        echo "请配置正确的内核头文件路径后重新运行构建脚本"
    fi

    show_usage_instructions
}

# 执行主函数
main "$@"