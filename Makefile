CC = gcc
LD = ld
CFLAGS = -m32 -ffreestanding -fno-pie -fno-stack-protector -Os
LDFLAGS = -m elf_i386 -T src/linker.ld

all: build/yoroos.iso

build/boot.bin: src/boot.S
	mkdir -p build
	$(CC) -c src/boot.S -o build/boot.o
	$(LD) -Ttext 0x7c00 --oformat binary build/boot.o -o build/boot.bin

build/kernel_entry.o: src/kernel_entry.S
	mkdir -p build
	$(CC) -m32 -c src/kernel_entry.S -o build/kernel_entry.o

build/kernel.o: src/kernel.c
	mkdir -p build
	$(CC) $(CFLAGS) -c src/kernel.c -o build/kernel.o

build/kernel.bin: build/kernel_entry.o build/kernel.o src/linker.ld
	$(LD) $(LDFLAGS) -o build/kernel.bin build/kernel_entry.o build/kernel.o

build/os.img: build/boot.bin build/kernel.bin
	dd if=/dev/zero of=build/os.img bs=1024 count=1440
	dd if=build/boot.bin of=build/os.img conv=notrunc
	dd if=build/kernel.bin of=build/os.img seek=1 conv=notrunc

build/yoroos.iso: build/os.img build_iso.py
	python3 build_iso.py

clean:
	rm -rf build/*.o build/*.bin build/*.img build/*.iso
