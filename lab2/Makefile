RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy

CFLAGS = -mcmodel=medany -ffreestanding -nostdlib -g -Wall
QEMU ?= qemu-system-riscv64
TARGET = kernel

build: clean
	$(CC) $(CFLAGS) -c *.S *.c
	$(LD) -T link.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin

run: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET).bin -initrd initramfs.cpio -display none -serial stdio

run-pty: build
	$(QEMU) -M virt -m 8G -kernel $(TARGET).bin -initrd initramfs.cpio -display none -serial pty

clean:
	rm -f $(TARGET).bin $(TARGET).elf *.o

.PHONY: build run clean
