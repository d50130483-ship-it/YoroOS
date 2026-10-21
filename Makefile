YOROOS_CC = gcc -m32 -ffreestanding -fno-pic -fno-pie -fno-stack-protector -fno-builtin \
            -mno-sse -mno-mmx -msoft-float -O2 -Wall -Wextra -Wno-unused-parameter -fno-asynchronous-unwind-tables \
            -Ikernel/include
AS = gcc -m32 -c

KSRC = kernel/src/klib.c kernel/src/hw.c kernel/src/ata.c kernel/src/fs.c \
       kernel/src/gfx.c kernel/src/ui.c kernel/src/apps.c kernel/src/installer.c \
       kernel/src/main.c kernel/font.c
KOBJ = $(patsubst %.c,build/%.o,$(KSRC))
KASM = build/entry.o build/isr.o

all: yoroos.img

build:
	mkdir -p build

build/entry.o: kernel/entry.S | build
	$(AS) $< -o $@
build/isr.o: kernel/isr.S | build
	$(AS) $< -o $@
build/%.o: %.c | build
	$(YOROOS_CC) -c $< -o $@

build/kernel.bin: $(KOBJ) $(KASM)
	python3 tools/flatlink.py build/entry.o build/isr.o $(filter-out build/entry.o build/isr.o,$(KOBJ)) \
		-o $@ --base 0x10000 --entry entry -M build/kernel.map

build/boot.o: boot/boot.S | build
	$(AS) $< -o $@
build/loader.o: boot/loader.S | build
	$(AS) $< -o $@
build/boot.bin: build/boot.o
	python3 tools/flatlink.py build/boot.o -o $@ --base 0x7C00 --entry _start
build/loader.bin: build/loader.o
	python3 tools/flatlink.py build/loader.o -o $@ --base 0x0600 --entry _start

yoroos.img: build/boot.bin build/loader.bin build/kernel.bin
	dd if=/dev/zero of=$@ bs=1M count=16 status=none
	dd if=build/boot.bin of=$@ bs=512 conv=notrunc status=none
	dd if=build/loader.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	dd if=build/kernel.bin of=$@ bs=512 seek=9 conv=notrunc status=none
	@echo "=== yoroos.img готов ($$(du -h $@ | cut -f1)) ==="

# ---- симулятор для разработки (рендерит кадры в PNG) ----
SIMSRC = tools/sim.c kernel/src/klib.c kernel/src/fs.c kernel/src/gfx.c \
         kernel/src/ui.c kernel/src/apps.c kernel/src/installer.c kernel/src/ata.c kernel/font.c
sim: $(SIMSRC) kernel/src/ata.c kernel/src/hw.c
	gcc -m64 -O1 -g -DYORO_SIM -Ikernel/include $(SIMSRC) -o build/sim

run-sim: sim
	./build/sim

clean:
	rm -rf build yoroos.img out/*.png

.PHONY: all clean sim run-sim
