CC = i686-elf-gcc
LD = i686-elf-gcc
COMMONFLAGS = -g -ffreestanding -nostdlib -fno-builtin -Wall -Wextra
CFLAGS = $(COMMONFLAGS)
LDFLAGS = $(COMMONFLAGS) -Tlinker.ld
ASFLAGS = -felf32

# clang setup example: (with ldd)
# CC = clang
# LD = clang
# COMMONFLAGS = -g -ffreestanding -nostdlib -fno-builtin --target=i386-none-elf -Wall -Wextra -fno-PIC -fno-PIE
# CFLAGS = $(COMMONFLAGS)
# LDFLAGS = $(COMMONFLAGS) -static -Tlinker.ld -z noexecstack -Wl,--build-id=none -fuse-ld=lld

BINARY = multitask.elf

all: $(BINARY)

$(BINARY): multitask.o multitask_asm.o
	$(LD) $(LDFLAGS) -o $@ $^

multitask.o: multitask.c
	$(CC) $(CFLAGS) -c $< -o $@

multitask_asm.o: multitask.asm
	nasm $(ASFLAGS) $< -o $@

run: all
	qemu-system-i386 -kernel $(BINARY)

drun: all
	qemu-system-i386 -s -S -kernel $(BINARY)

debug:
	gdb --symbols=$(BINARY) -ex 'target remote localhost:1234'

clean:
	rm -f *.o $(BINARY)
