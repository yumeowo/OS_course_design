#!/bin/bash

# MyFS开发环境设置脚本 - 专为WSL环境设计

echo "=== MyFS开发环境设置 (WSL) ==="

# 检查是否在WSL环境中
check_wsl() {
    if [[ ! -f /proc/version ]] || ! grep -qi "microsoft\|wsl" /proc/version; then
        echo "错误：此脚本仅适用于WSL环境"
        echo "请在Windows Subsystem for Linux中运行此脚本"
        exit 1
    fi

    echo "✓ 检测到WSL环境"

    # 显示WSL版本信息
    if command -v wsl.exe >/dev/null 2>&1; then
        echo "WSL版本信息："
        cat /proc/version | grep -o "Microsoft.*" || echo "WSL 1.x"
    fi
}

# 安装基本开发工具
install_basic_tools() {
    echo "正在安装基本开发工具..."

    # 更新包列表
    sudo apt update

    # 安装必要的包
    sudo apt install -y \
        build-essential \
        cmake \
        git \
        tree \
        htop

    echo "✓ 基本开发工具安装完成"
}

# 检查内核头文件状态
check_kernel_headers() {
    echo ""
    echo "=== 内核头文件检查 ==="

    WSL_KERNEL_VERSION=$(uname -r)
    DEFAULT_HEADERS_PATH="/lib/modules/$WSL_KERNEL_VERSION/build"

    echo "WSL内核版本: $WSL_KERNEL_VERSION"
    echo "默认头文件路径: $DEFAULT_HEADERS_PATH"

    # 检查默认路径
    if [ -d "$DEFAULT_HEADERS_PATH" ]; then
        echo "✓ 在默认路径找到内核头文件"
        return 0
    fi

    # 检查环境变量中的自定义路径
    if [ -n "$KERNEL_DIR" ] && [ -d "$KERNEL_DIR" ]; then
        echo "✓ 在环境变量KERNEL_DIR中找到内核头文件: $KERNEL_DIR"
        return 0
    fi

    # 检查是否存在配置文件
    if [ -f ".kernel_path" ]; then
        SAVED_KERNEL_PATH=$(cat .kernel_path)
        if [ -d "$SAVED_KERNEL_PATH" ]; then
            echo "✓ 在配置文件中找到内核头文件: $SAVED_KERNEL_PATH"
            return 0
        fi
    fi

    echo "⚠ 未找到内核头文件"
    show_kernel_headers_help
    ask_for_custom_kernel_path
}

# 显示内核头文件帮助信息
show_kernel_headers_help() {
    echo ""
    echo "WSL内核头文件需要手动安装。请按照以下步骤操作："
    echo ""
    echo "1. 确定WSL内核版本："
    echo "   uname -r"
    echo ""
    echo "2. 下载对应的内核源码和头文件："
    echo "3. 配置内核构建环境"
    echo "注意：由于WSL使用定制内核，标准的linux-headers包可能不完全兼容"
    echo "建议查阅WSL官方文档获取最新的内核开发指南"
}

# 询问用户是否要设置自定义内核路径
ask_for_custom_kernel_path() {
    echo ""
    read -p "是否要设置自定义内核头文件路径？(y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        set_custom_kernel_path
    else
        echo "跳过自定义内核路径设置"
        echo "您可以稍后通过以下方式设置："
        echo "1. 设置环境变量: export KERNEL_DIR=/path/to/kernel/headers"
        echo "2. 创建配置文件: echo '/path/to/kernel/headers' > .kernel_path"
        echo "3. 重新运行此脚本"
    fi
}

# 设置自定义内核路径
set_custom_kernel_path() {
    echo ""
    echo "请输入内核头文件路径："
    read -p "路径: " custom_path

    if [ -z "$custom_path" ]; then
        echo "路径不能为空"
        return 1
    fi

    # 展开波浪线
    custom_path="${custom_path/#\~/$HOME}"

    if [ ! -d "$custom_path" ]; then
        echo "警告：路径 '$custom_path' 不存在"
        read -p "是否仍要保存此路径？(y/N): " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            echo "取消设置自定义路径"
            return 1
        fi
    fi

    # 保存到配置文件
    echo "$custom_path" > .kernel_path
    echo "✓ 自定义内核路径已保存到 .kernel_path 文件"
    echo "路径: $custom_path"

    # 提示设置环境变量
    echo ""
    echo "建议将以下行添加到您的 ~/.bashrc 或 ~/.zshrc 文件中："
    echo "export KERNEL_DIR=\"$custom_path\""
    echo ""
    read -p "是否自动添加到 ~/.bashrc？(y/N): " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "export KERNEL_DIR=\"$custom_path\"" >> ~/.bashrc
        echo "✓ 已添加到 ~/.bashrc"
        echo "请运行 'source ~/.bashrc' 或重新打开终端使其生效"
    fi
}

# 设置项目权限和目录
setup_project() {
    echo ""
    echo "=== 项目设置 ==="
    
    # 设置脚本执行权限
    chmod +x scripts/*.sh
    echo "✓ 设置脚本执行权限"
    
    # 创建必要的目录
    mkdir -p build
    mkdir -p logs
    echo "✓ 创建项目目录"
    
    # 检查项目文件结构
    echo "项目文件结构："
    tree -L 2 . 2>/dev/null || ls -la
}

# 显示下一步操作指南
show_next_steps() {
    echo ""
    echo "=== 环境设置完成 ==="
    echo ""
    echo "下一步操作："
    echo "1. 手动安装WSL内核头文件（如果尚未安装）"
    echo "2. 运行构建脚本："
    echo "   ./scripts/build.sh"
    echo ""
    echo "开发工作流程："
    echo "- 编辑代码：使用您喜欢的编辑器"
    echo "- 构建项目：./scripts/build.sh"
    echo "- 测试驱动：cd driver && make load"
    echo "- 挂载文件系统：./scripts/mount.sh"
    echo ""
    echo "注意事项："
    echo "- 内核模块操作需要sudo权限"
    echo "- 确保WSL版本支持内核模块加载"
    echo "- 建议在开发过程中定期备份代码"
}

# 主函数
main() {
    check_wsl
    install_basic_tools
    check_kernel_headers
    setup_project
    show_next_steps
}

# 执行主函数
main