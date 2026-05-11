# NYCU Operating Systems Capstone

Course assignments and implementations for the **Operating Systems Capstone** course at NYCU.

## Contents

- `lab0` - Environment Setup and Cross-compilation
- `lab1` - Bare-metal Programming and UART
- `lab2` - Bootloader and Kernel Loading
- `lab3` - Memory Allocation and Buddy System
- `lab4` - Exception Handling, Interrupts, Timer, and Basic Multitasking
- `lab5` - Threads, User Processes, System Calls, Signals, and Display

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

Later labs further introduce core operating system mechanisms, including exception and interrupt handling, timer-based scheduling, kernel threads, user process execution, system calls, process control, signal handling, and basic framebuffer display support.

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

## Note

This repository is still being updated as the course progresses.
