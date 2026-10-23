# YoroOS build system
# Everything is built from source: boot sector, stage2, kernel, ISO.

CC      := gcc
AS      := as
LD      := ld
OBJCOPY := objcopy

CFLAGS := -m32 -std=gnu99 -O2 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
          -fno-asynchronous-unwind-tables -fno-builtin -fno-common -mno-sse -mno-mmx \
          -mno-80387 -Wall -Wextra -Wno-unused-parameter -Iinclude
LDFLAGS := -m elf_i386 -T boot/kernel.ld --build-id=none -nostdlib

KERNEL_SRC := kernel/start.c kernel/string.c kernel/heap.c kernel/gfx.c \
              kernel/font.c kernel/fs.c kernel/theme.c kernel/wm.c kernel/ui.c \
              kernel/apps.c kernel/app_explorer.c kernel/app_terminal.c \
              kernel/app_settings.c kernel/app_browser.c kernel/app_store.c \
              kernel/app_misc.c kernel/icons.c kernel/desktop.c kernel/main.c \
              kernel/font_data.c kernel/ata.c kernel/inflate.c \
              kernel/app_zip.c kernel/app_installer.c
KERNEL_OBJ := $(patsubst kernel/%.c,build/%.o,$(KERNEL_SRC))

.PHONY: all clean iso image run

all: build/yoroos.iso

build:
	@mkdir -p build

build/%.o: kernel/%.c include/kernel.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build/entry.o: kernel/entry.asm | build
	$(AS) --32 $< -o $@

build/isr.o: kernel/isr.asm | build
	$(AS) --32 $< -o $@

build/kernel.elf: $(KERNEL_OBJ) build/entry.o build/isr.o | build
	$(LD) $(LDFLAGS) -o $@ $^
	@size $@

build/kernel.bin: build/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@ls -l $@

build/boot.bin: boot/boot.asm | build
	$(AS) --32 $< -o build/boot.o
	$(OBJCOPY) -O binary -j .text build/boot.o $@
	@test $$(stat -c%s $@) -eq 512 || (echo "boot sector must be 512 bytes"; exit 1)

build/stage2.bin: boot/stage2.asm | build
	$(AS) --32 $< -o build/stage2.o
	$(OBJCOPY) -O binary -j .text build/stage2.o $@
	@test $$(stat -c%s $@) -le 2048 || (echo "stage2 must fit in the 2 KiB block the boot sector relies on"; exit 1)
	@test $$((0x$$(nm build/stage2.o | awk '/ t stage2_main$$/ {print $$1}'))) -le 512 || \
		(echo "stage2: self-loader must fit into the first sector"; exit 1)

build/yoroos.img: build/boot.bin build/stage2.bin build/kernel.bin tools/mkimage.py
	python3 tools/mkimage.py build/boot.bin build/stage2.bin build/kernel.bin $@

build/yoroos.iso: build/yoroos.img tools/mkiso.py README.md docs/README-ISO.txt
	python3 tools/mkiso.py build/yoroos.img $@ YOROOS README.md docs/README-ISO.txt
	@ls -lh $@

image: build/yoroos.img
iso: build/yoroos.iso

run: build/yoroos.iso
	qemu-system-i386 -cdrom build/yoroos.iso -m 256 -vga std

clean:
	rm -rf build
