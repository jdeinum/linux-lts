# Makefile for compiling a kernel module

# Path to the Linux source code
KERNEL_SRC := ~/Downloads/linux/linux-lts

# Name of your kernel module
MODULE_NAME := safe_shutdown

# Set the current directory to the Linux source directory
obj-m := $(MODULE_NAME).o
PWD := $(shell pwd)

# Target to build the module
all:
	make -C $(KERNEL_SRC) M=$(PWD) modules

# Target to clean up the build artifacts
clean:
	make -C $(KERNEL_SRC) M=$(PWD) clean
