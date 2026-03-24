RISCV_GNU ?= riscv64-unknown-elf
CC = $(RISCV_GNU)-gcc
LD = $(RISCV_GNU)-ld
OBJCOPY = $(RISCV_GNU)-objcopy
CFLAGS = -mcmodel=medany -fno-pie -ffreestanding -nostdlib -g -Wall
QEMU ?= qemu-system-riscv64
TARGET = test_kernel

build: clean
	$(CC) $(CFLAGS) -c *.S *.c
	$(LD) -T link.ld -o $(TARGET).elf *.o
	$(OBJCOPY) -O binary $(TARGET).elf $(TARGET).bin

run: build $(TARGET).bin
	$(QEMU) -M virt -m 8G -kernel $(TARGET).bin -display none -serial stdio

clean:
	rm -f $(TARGET).bin $(TARGET).elf *.o
