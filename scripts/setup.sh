#!/bin/bash

# --- 配置 ---
# 设置磁盘镜像文件名
IMAGE_FILE="../mydisk.img"
# 设置磁盘镜像大小 (单位: MB)
DISK_SIZE_MB=32
# 设置格式化工具的源文件名和目标可执行文件名
MKFS_SOURCE="../tools/mkfs.c"
MKFS_TOOL="../tools/mkfs"
# 包含结构体定义的头文件
HEADER_FILE="../driver/fs.h"

# 设置颜色代码
GREEN='\033[0;32m'
NC='\033[0m' # No Color

echo -e "${GREEN}--- 步骤 1: 检查所需文件 ---${NC}"
if [ ! -f "$MKFS_SOURCE" ]; then
    echo "错误: 格式化工具源码 '$MKFS_SOURCE' 未找到!"
    exit 1
fi
if [ ! -f "$HEADER_FILE" ]; then
    echo "错误: 头文件 '$HEADER_FILE' 未找到!"
    exit 1
fi
echo "所有源文件均已找到。"

echo -e "\n${GREEN}--- 步骤 2: 创建磁盘镜像文件 ($IMAGE_FILE) ---${NC}"
# 使用 dd 命令创建一个用零填充的空文件
# bs=1M 表示块大小为1MB, count=$DISK_SIZE_MB 表示块的数量
dd if=/dev/zero of=$IMAGE_FILE bs=1M count=$DISK_SIZE_MB
echo "成功创建大小为 ${DISK_SIZE_MB}MB 的磁盘镜像。"

echo -e "\n${GREEN}--- 步骤 3: 编译格式化工具 ---${NC}"
# 使用 gcc 编译 C 源文件，-o 指定输出的可执行文件名
gcc $MKFS_SOURCE -o $MKFS_TOOL
if [ $? -eq 0 ]; then
    echo "格式化工具 '$MKFS_TOOL' 编译成功。"
else
    echo "错误: 格式化工具编译失败!"
    exit 1
fi

echo -e "\n${GREEN}--- 步骤 4: 格式化磁盘镜像 ---${NC}"
# 运行格式化工具，目标为我们创建的镜像文件
./$MKFS_TOOL $IMAGE_FILE
if [ $? -eq 0 ]; then
    echo -e "\n${GREEN}--- 设置完成! ---${NC}"
    echo "磁盘镜像 '$IMAGE_FILE' 已准备就绪，可以使用了。"
else
    echo "错误: 磁盘格式化失败!"
    exit 1
fi