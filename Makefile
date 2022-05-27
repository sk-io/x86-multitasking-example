CC = i686-elf-gcc
LD = i686-elf-ld
CFLAGS = -g -nostdlib -ffreestanding -m32 -c -Wall -Wextra
LDFLAGS = -g -Tlinker.ld -m elf_i386
ASFLAGS = -felf32

SOURCES_C = $(patsubst %.c, %.o, $(wildcard *.c))
SOURCES_ASM = $(patsubst %.asm, %.o, $(wildcard *.asm))

OBJ = $(SOURCES_ASM) $(SOURCES_C)

all: $(OBJ) link

link:
	$(LD) $(LDFLAGS) -o kernel.bin $(OBJ)

%.o:%.c
	$(CC) $(CFLAGS) $< -o $@

%.o:%.asm
	nasm $(ASFLAGS) $< -o $@

run: all
	qemu-system-i386 -kernel kernel.bin

drun: all
	qemu-system-i386 -s -S -kernel kernel.bin

debug:
	gdb --symbols=kernel.bin -ex 'target remote localhost:1234'

clean:
	rm -f *.o kernel.bin
