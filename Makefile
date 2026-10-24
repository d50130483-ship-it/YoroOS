# YoroOS: new kernel (rebuilt from scratch). UEFI x86_64 only.
#
#   make            build build/YoroOS-$(VERSION).iso (UEFI-bootable ISO for Hyper-V Gen 2 and UEFI PCs)
#   make efi        build only build/BOOTX64.EFI
#   make cursor     regenerate src/cursor_data.h and cursor/preview.png
#   make check      build the kernel and render it in the UEFI emulator (build/check.png)
#   make test       host tests for the HID parser

ZIG    := python3 -m ziglang
VERSION := 0.3.0-dev
ISO    := build/YoroOS-$(VERSION).iso

CFLAGS := -target x86_64-uefi-msvc -std=gnu11 -O2 -ffreestanding \
          -fno-stack-protector -mno-stack-arg-probe -mno-red-zone \
          -Wall -Wextra -Werror -Wno-unused-parameter -Isrc

SRCS := src/main.c src/hv_mouse.c src/hid.c
OBJS := build/main.obj build/hv_mouse.obj build/hid.obj
HDRS := src/efi.h src/cursor_data.h src/hv_mouse.h src/hid.h

.PHONY: all efi cursor iso check test clean

all: iso

efi: build/BOOTX64.EFI

build:
	mkdir -p build

build/%.obj: src/%.c $(HDRS) | build
	$(ZIG) cc $(CFLAGS) -c -o $@ $<

# lld-link is used directly: zig cc ignores /base, and the image must have a
# non-zero ImageBase with DYNAMICBASE so the firmware can place it.
build/BOOTX64.EFI: $(OBJS)
	$(ZIG) lld-link /subsystem:efi_application /entry:EfiMain /nodefaultlib \
	  /base:0x10000 /dynamicbase /fixed:no /out:$@ $(OBJS)
	@ls -l $@

cursor: src/cursor_data.h

src/cursor_data.h: tools/mkcursor.py
	python3 tools/mkcursor.py

iso: $(ISO)

$(ISO): build/BOOTX64.EFI tools/mkiso.py
	python3 tools/mkiso.py build/BOOTX64.EFI $@

check: build/BOOTX64.EFI
	python3 tools/render_test.py build/BOOTX64.EFI build/check.png

test: build/test_hid
	./build/test_hid

build/test_hid: tools/test_hid.c src/hid.c src/hid.h src/efi.h | build
	$(ZIG) cc -target x86_64-linux-gnu -std=gnu11 -Isrc -Wall -Wextra -Werror -Wno-unused-parameter -o $@ tools/test_hid.c src/hid.c

clean:
	rm -rf build
