CC      := ia16-elf-gcc
LD      := ia16-elf-ld
NASM    := nasm

CFLAGS  := -ffreestanding -Os -Wall -Wextra -fno-pic -fno-builtin -fno-stack-protector
LDFLAGS := -T linker.ld -nostdlib -N

BUILD_DIR := build
OUT_DIR   := out

BOOT_BIN   := $(BUILD_DIR)/boot.bin
BIOS_READ_O := $(BUILD_DIR)/bios_read_sector.o
KERNEL_O   := $(BUILD_DIR)/kernel.o
KERNEL_ELF := $(BUILD_DIR)/kernel.elf
KERNEL_BIN := $(BUILD_DIR)/kernel.bin
IMG        := $(OUT_DIR)/bubbles.img

KERNEL_SECTORS ?= 20

.PHONY: all clean run info

all: $(IMG)

$(BUILD_DIR) $(OUT_DIR):
	mkdir -p $@

$(BOOT_BIN): boot.asm | $(BUILD_DIR)
	$(NASM) -f bin -D KERNEL_SECTORS=$(KERNEL_SECTORS) -o $@ $<

$(BIOS_READ_O): bios_read_sector.asm | $(BUILD_DIR)
	$(NASM) -f elf -o $@ $<

$(KERNEL_O): kernel.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(KERNEL_ELF): $(KERNEL_O) $(BIOS_READ_O) linker.ld | $(BUILD_DIR)
	$(LD) $(LDFLAGS) -o $@ $(KERNEL_O) $(BIOS_READ_O)

$(KERNEL_BIN): $(KERNEL_ELF)
	cp $(KERNEL_ELF) $@

$(IMG): $(BOOT_BIN) $(KERNEL_BIN) | $(OUT_DIR)
	dd if=/dev/zero of=$@ bs=512 count=2880 status=none
	dd if=$(BOOT_BIN) of=$@ conv=notrunc bs=512 count=1 status=none
	dd if=$(KERNEL_BIN) of=$@ conv=notrunc bs=512 seek=1 status=none
	$(MAKE) info

info:
	@echo "kernel.bin size: $$(stat -c%s $(KERNEL_BIN) 2>/dev/null || stat -f%z $(KERNEL_BIN)) bytes"
	@echo "KERNEL_SECTORS=$(KERNEL_SECTORS)"

run: $(IMG)
	qemu-system-i386 -drive file=$(IMG),format=raw,if=floppy -boot a -no-reboot -no-shutdown -serial stdio

clean:
	rm -rf $(BUILD_DIR) $(OUT_DIR)
