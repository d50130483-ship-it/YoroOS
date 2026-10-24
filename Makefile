# YoroOS: new kernel (rebuilt from scratch). UEFI x86_64 only.
#
#   make            build build/YoroOS.iso (UEFI-bootable ISO for Hyper-V Gen 2 and UEFI PCs)
#   make efi        build only build/BOOTX64.EFI
#   make cursor     regenerate src/cursor_data.h and cursor/preview.png
#   make check      build the kernel and render it in the UEFI emulator (build/check.png)

ZIG    := python3 -m ziglang
VERSION := 0.3.0-dev
ISO    := build/YoroOS-$(VERSION).iso

CFLAGS := -target x86_64-uefi-msvc -std=gnu11 -O2 -ffreestanding \
          -fno-stack-protector -mno-stack-arg-probe -mno-red-zone \
          -Wall -Wextra -Werror -Wno-unused-parameter -Isrc

SRC := src/main.c src/efi.h src/cursor_data.h

.PHONY: all efi cursor iso check clean

all: iso

efi: build/BOOTX64.EFI

build:
	mkdir -p build

build/BOOTX64.EFI: $(SRC) | build
	$(ZIG) cc $(CFLAGS) -nostdlib -o $@ src/main.c
	@ls -l $@

cursor: src/cursor_data.h

src/cursor_data.h: tools/mkcursor.py
	python3 tools/mkcursor.py

iso: $(ISO)

$(ISO): build/BOOTX64.EFI tools/mkiso.py
	python3 tools/mkiso.py build/BOOTX64.EFI $@

check: build/BOOTX64.EFI
	python3 tools/render_test.py build/BOOTX64.EFI build/check.png

clean:
	rm -rf build
