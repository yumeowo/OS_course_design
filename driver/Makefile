# MyFS内核驱动模块Makefile

# 目标模块名
obj-m += myfs.o

# 模块包含的源文件（注意：不包含.c扩展名）
myfs-objs := myfs_main.o cache.o super.o

# 内核构建目录设置（支持多种方式）
# 优先级：命令行参数 > 环境变量 > 配置文件 > 默认路径

# 1. 首先尝试从配置文件读取
ifneq ($(wildcard ../.kernel_path),)
    KERNEL_DIR_FROM_FILE := $(shell cat ../.kernel_path 2>/dev/null)
endif

# 2. 设置KERNEL_DIR（按优先级）
ifndef KERNEL_DIR
    ifdef KERNEL_DIR_FROM_FILE
        KERNEL_DIR := $(KERNEL_DIR_FROM_FILE)
    else
        KERNEL_DIR := /lib/modules/$(shell uname -r)/build
    endif
endif

# 展开用户目录符号
KERNEL_DIR := $(shell echo $(KERNEL_DIR) | sed 's|^~|$(HOME)|')

# 当前目录
PWD := $(shell pwd)

# 编译标志
ccflags-y := -Wall -Wextra -std=gnu11 -DDEBUG

# 默认目标：编译模块
all:
	@echo "Building MyFS kernel module..."
	@echo "Using kernel directory: $(KERNEL_DIR)"
	@if [ ! -d "$(KERNEL_DIR)" ]; then \
		echo "Error: Kernel directory does not exist: $(KERNEL_DIR)"; \
		echo "Please set KERNEL_DIR or create .kernel_path file"; \
		echo "Example: make KERNEL_DIR=/path/to/kernel/headers"; \
		exit 1; \
	fi
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules

# 清理目标
clean:
	@echo "Cleaning MyFS kernel module..."
	@if [ -d "$(KERNEL_DIR)" ]; then \
		$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean; \
	fi
	rm -f *.o *.ko *.mod.c *.mod *.order *.symvers

# 安装模块到系统
install: all
	@echo "Installing MyFS kernel module..."
	sudo $(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules_install
	sudo depmod -a

# 加载模块
load: all
	@echo "Loading MyFS kernel module..."
	sudo insmod myfs.ko

# 卸载模块
unload:
	@echo "Unloading MyFS kernel module..."
	sudo rmmod myfs || true

# 重新加载模块
reload: unload load

# 查看模块信息
info:
	@echo "MyFS module information:"
	@echo "Kernel directory: $(KERNEL_DIR)"
	@if [ -f "myfs.ko" ]; then \
		modinfo myfs.ko; \
	else \
		echo "Module not built yet. Run 'make' first."; \
	fi

# 查看内核日志
log:
	@echo "Recent kernel messages:"
	dmesg | tail -20

# 检查内核目录
check:
	@echo "Checking kernel configuration..."
	@echo "Kernel directory: $(KERNEL_DIR)"
	@if [ -d "$(KERNEL_DIR)" ]; then \
		echo "✓ Kernel directory exists"; \
		if [ -f "$(KERNEL_DIR)/include/linux/module.h" ]; then \
			echo "✓ Module headers found"; \
		elif [ -f "$(KERNEL_DIR)/include/linux/kernel.h" ]; then \
			echo "✓ Kernel headers found"; \
		else \
			echo "⚠ Warning: Kernel headers may be incomplete"; \
		fi; \
		echo "Kernel version: $$(cat $(KERNEL_DIR)/include/config/kernel.release 2>/dev/null || echo 'Unknown')"; \
	else \
		echo "✗ Kernel directory not found"; \
		echo "Set KERNEL_DIR or create ../.kernel_path file"; \
	fi

# 帮助信息
help:
	@echo "Available targets:"
	@echo "  all       - Build the kernel module"
	@echo "  clean     - Clean build files"
	@echo "  install   - Install module to system"
	@echo "  load      - Load the module"
	@echo "  unload    - Unload the module"
	@echo "  reload    - Unload and load the module"
	@echo "  info      - Show module information"
	@echo "  log       - Show recent kernel messages"
	@echo "  check     - Check kernel directory configuration"
	@echo "  help      - Show this help"
	@echo ""
	@echo "Kernel directory configuration:"
	@echo "  Current: $(KERNEL_DIR)"
	@echo ""
	@echo "Ways to specify kernel directory (in priority order):"
	@echo "  1. Command line: make KERNEL_DIR=/path/to/headers"
	@echo "  2. Environment:  export KERNEL_DIR=/path/to/headers"
	@echo "  3. Config file:  echo '/path/to/headers' > ../.kernel_path"
	@echo "  4. Default:      /lib/modules/\$$(uname -r)/build"

.PHONY: all clean install load unload reload info log check help