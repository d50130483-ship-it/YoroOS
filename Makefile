# YoroOS 0.2 «Aurora» — Dual-Architecture Build System
# Builds BOTH:
#   1) Native 64-bit UEFI Kernel Application (build/BOOTX64.EFI) for Hyper-V Gen 2 / UEFI PCs
#   2) Native 32-bit BIOS Protected-Mode Kernel (build/kernel32.bin) for BIOS / Hyper-V Gen 1 / v86
#   3) Standalone Quad-Boot Hybrid ISO image (release/YoroOS-0.2.0.iso)

ZIG     := python3 -m ziglang
CC32    := gcc
CXX32   := g++
AS      := as
LD      := ld
OBJCOPY := objcopy

COMMON_WARN := -Wall -Wextra -Werror -Wno-unused-parameter -Iinclude

CFLAGS64   := -target x86_64-uefi-msvc -std=gnu11 -O2 -ffreestanding -fno-stack-protector \
              -mno-stack-arg-probe -mno-red-zone $(COMMON_WARN)
CXXFLAGS64 := -target x86_64-uefi-msvc -std=c++17 -O2 -ffreestanding -fno-exceptions -fno-rtti \
              -fno-stack-protector -mno-stack-arg-probe -mno-red-zone \
              -Wno-unused-command-line-argument $(COMMON_WARN)

CFLAGS32   := -m32 -std=gnu11 -O2 -ffreestanding -fno-pic -fno-pie -fno-stack-protector \
              -fno-asynchronous-unwind-tables -fno-builtin -fno-common \
              -mno-sse -mno-mmx -mno-80387 $(COMMON_WARN)
CXXFLAGS32 := -m32 -std=c++17 -O2 -ffreestanding -fno-exceptions -fno-rtti -fno-pic -fno-pie \
              -fno-stack-protector -fno-asynchronous-unwind-tables -fno-common \
              -mno-sse -mno-mmx -mno-80387 $(COMMON_WARN)
LDFLAGS32  := -m elf_i386 -T boot/kernel.ld --build-id=none -nostdlib --no-warn-rwx-segments -z noexecstack

CORE_C := string heap font font_data assets_data gfx theme icons inflate pe fs diskfmt ui wm \
          apps main desktop app_installer app_explorer app_zip app_terminal \
          app_settings app_browser app_store app_misc

U64_OBJS := $(patsubst %,build/u64/%.obj,$(CORE_C)) \
            build/u64/fx.obj build/u64/hv_mouse.obj build/u64/hv_native.obj build/u64/hal_uefi64.obj build/u64/pe_asm.obj

B32_OBJS := $(patsubst %,build/b32/%.o,$(CORE_C)) \
            build/b32/fx.o build/b32/hv_native.o build/b32/ata.o build/b32/hal_bios32.o \
            build/b32/entry.o build/b32/isr.o build/b32/pe_asm.o

ISO_OUT  := release/YoroOS-0.2.9.iso

.PHONY: all clean seeds iso

all: $(ISO_OUT)

build/dirs:
	@mkdir -p build/u64 build/b32 release
	@touch build/dirs

kernel/font_data.c: tools/mkfont.py
	python3 tools/mkfont.py

kernel/fs_seed.h: tools/mkseed.py
	python3 tools/mkseed.py

# --- 64-bit UEFI Generation 2 build rules ---
build/u64/%.obj: kernel/%.c include/kernel.h include/efi.h kernel/fs_seed.h | build/dirs
	$(ZIG) cc $(CFLAGS64) -c $< -o $@

build/u64/fx.obj: kernel/fx.cpp include/kernel.h | build/dirs
	$(ZIG) c++ $(CXXFLAGS64) -c $< -o $@

build/u64/pe_asm.obj: kernel/pe_asm.S | build/dirs
	$(ZIG) cc -target x86_64-uefi-msvc -c $< -o $@

build/BOOTX64.EFI: $(U64_OBJS)
	$(ZIG) cc -target x86_64-uefi-msvc -nostdlib $(U64_OBJS) -o $@
	@ls -lh $@

# --- 32-bit Legacy BIOS / Generation 1 build rules ---
build/b32/%.o: kernel/%.c include/kernel.h kernel/fs_seed.h | build/dirs
	$(CC32) $(CFLAGS32) -c $< -o $@

build/b32/fx.o: kernel/fx.cpp include/kernel.h | build/dirs
	$(CXX32) $(CXXFLAGS32) -c $< -o $@

build/b32/pe_asm.o: kernel/pe_asm.S | build/dirs
	$(CC32) -m32 -c $< -o $@

build/b32/entry.o: kernel/entry.asm | build/dirs
	$(AS) --32 $< -o $@

build/b32/isr.o: kernel/isr.asm | build/dirs
	$(AS) --32 $< -o $@

build/kernel32.elf: $(B32_OBJS)
	$(LD) $(LDFLAGS32) -o $@ $(B32_OBJS)
	@size $@

build/kernel32.bin: build/kernel32.elf
	$(OBJCOPY) -O binary $< $@
	@ls -lh $@

build/boot.bin: boot/boot.asm | build/dirs
	$(AS) --32 $< -o build/boot.o
	$(OBJCOPY) -O binary -j .text build/boot.o $@
	@test $$(stat -c%s $@) -eq 512

build/stage2.bin: boot/stage2.asm | build/dirs
	$(AS) --32 $< -o build/stage2.o
	$(OBJCOPY) -O binary -j .text build/stage2.o $@
	@test $$(stat -c%s $@) -le 2048

build/yoroos.img: build/boot.bin build/stage2.bin build/kernel32.bin tools/mkimage.py
	python3 tools/mkimage.py build/boot.bin build/stage2.bin build/kernel32.bin $@

$(ISO_OUT): build/yoroos.img build/BOOTX64.EFI build/boot.bin build/kernel32.bin tools/mkiso.py README.md
	python3 tools/mkiso.py build/yoroos.img build/BOOTX64.EFI build/boot.bin build/kernel32.bin $@ YOROOS_0_2 README.md
	@cp $@ build/yoroos.iso
	@ls -lh $@

iso: $(ISO_OUT)

clean:
	rm -rf build
