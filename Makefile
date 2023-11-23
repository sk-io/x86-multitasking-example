CC = i686-elf-gcc # you can use clang too if you dont want to build your own cross compiler
LD = i686-elf-ld
CFLAGS = -g -nostdlib -ffreestanding -m32 -c -Wall -Wextra
LDFLAGS = -g -Tlinker.ld -m elf_i386
ASFLAGS = -felf32

SOURCES_C = $(patsubst %.c, %.o, $(wildcard *.c))
SOURCES_ASM = $(patsubst %.asm, %.o, $(wildcard *.asm))

OBJ = $(SOURCES_ASM) $(SOURCES_C)
BINARY = kernel.bin

all: $(BINARY)

$(BINARY): $(OBJ)
	$(LD) $(LDFLAGS) -o $(BINARY) $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

%.o: %.asm
	nasm $(ASFLAGS) $< -o $@

run: all
	qemu-system-i386 -kernel $(BINARY)

drun: all
	qemu-system-i386 -s -S -kernel $(BINARY)

debug:
	gdb --symbols=$(BINARY) -ex 'target remote localhost:1234'

clean:
	rm -f *.o $(BINARY)
