# NYCU Operating Systems Capstone

Course assignments and implementations for the **Operating Systems Capstone** course at NYCU.

## Contents

- `lab0` - Environment Setup and Cross-compilation
- `lab1` - Bare-metal Programming and UART
- `lab2` - Bootloader and Kernel Loading
- `lab3` - Memory Allocation and Buddy System
- `lab4` - Exception Handling, Interrupts, Timer, and Basic Multitasking
- `lab5` - Threads, User Processes, System Calls, Signals, and Display
- `lab6` - Virtual Memory, MMU, Address Space Isolation, Demand Paging, and Copy-on-Write
- `lab7` - Virtual File System, tmpfs, ramfs, devfs, and Device Files

## Topics Covered

- RISC-V
- Bare-metal Programming
- UART Communication
- Linker Script
- Bootloader
- Kernel Loading
- Device Tree
- Initramfs
- Page Frame Allocator
- Dynamic Memory Allocator
- Buddy System
- Exception and Interrupt Handling
- Timer Interrupt
- Context Switching
- Thread Scheduling
- User Process Management
- System Calls
- Process Control
- Signal Handling
- Framebuffer Display
- Virtual Memory
- RISC-V Sv39 Paging
- MMU Initialization
- Page Table Management
- Kernel Higher-Half Mapping
- User Address Space Isolation
- Page Fault Handling
- Demand Paging
- Copy-on-Write
- Virtual File System
- Vnode and File Handle Abstractions
- tmpfs Root File System
- Read-only ramfs from Initramfs
- devfs Device File System
- UART Device File
- Framebuffer Device File
- `lseek64` and `ioctl`
- QEMU

## Tech Stack

- C
- RISC-V Assembly
- RISC-V
- QEMU
- Makefile

## Description

This repository contains operating systems labs focused on low-level system programming on RISC-V platforms.
The project starts from bare-metal initialization and UART communication, then extends to bootloader implementation, kernel loading, device tree parsing, initramfs support, and kernel memory management.

Later labs further introduce core operating system mechanisms, including exception and interrupt handling, timer-based scheduling, kernel threads, user process execution, system calls, process control, signal handling, basic framebuffer display support, virtual memory, demand paging, and copy-on-write.

The latest lab focuses on virtual file systems. It introduces a VFS layer with vnode and file handle abstractions, mounts tmpfs as the root file system, provides a read-only ramfs backed by initramfs, and implements devfs device files for UART and framebuffer access through standard file operations.

## Current Progress

Implemented components include:

- Bare-metal kernel initialization
- UART driver and shell interface
- UART-based bootloader
- Kernel loading flow
- Device tree parsing
- Initramfs file listing and loading
- Page frame allocator
- Dynamic memory allocator
- Buddy system
- Timer interrupt handling
- Cooperative / timer-driven scheduling
- Kernel thread creation and context switching
- User process loading and execution
- Basic system calls such as `getpid`, `uart_read`, `uart_write`, `exec`, `fork`, `waitpid`, and `exit`
- Process control and signal handling
- Basic framebuffer display support
- RISC-V Sv39 virtual memory support
- MMU initialization using `satp`
- Kernel higher-half address mapping
- Identity mapping during early boot and identity map removal
- Three-level page table management
- Per-process page table allocation
- User code and user stack mapping
- Address space switching during context switch
- Reimplementation of `fork` and `exec` with isolated virtual address spaces
- Page fault handling
- Anonymous memory mapping with `mmap`
- Demand paging
- Copy-on-write support for `fork`
- VFS registration, lookup, mount, open, close, read, and write
- Current working directory and per-process file descriptor table
- VFS system calls for `open`, `close`, `read`, `write`, `mkdir`, `mount`, and `chdir`
- tmpfs root file system with regular files and directories
- Read-only ramfs mounted at `/ramfs`
- devfs mounted at `/dev`
- UART device file at `/dev/uart`
- Standard input, output, and error mapped to `/dev/uart`
- Framebuffer device file at `/dev/fb`
- `lseek64` and `ioctl` support for framebuffer access
- User pointer translation for framebuffer `ioctl`
