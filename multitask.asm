; ----- Multiboot Header -----

section .multiboot
	MB_MAGIC    equ 0x1BADB002
	MB_FLAGS    equ 0
	MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

	dd MB_MAGIC
	dd MB_FLAGS
	dd MB_CHECKSUM

; ----- Initial Stack -----

section .bss
stack_max:
	resb 16384 ; reserve 16 KiB for the initial kernel stack
stack_bottom:

; ----- Boot -----

section .text
global _start
_start:
	; interrupts are disabled

	mov esp, stack_bottom

	; push ebx ; multiboot info pointer, we don't need it though

	extern kernel_main
	call kernel_main
 
	cli
.hang:
	hlt
	jmp .hang

; ----- GDT -----

global load_gdt
load_gdt:
	mov eax, [esp + 4]
	lgdt [eax]

	mov ax, 0x10
	mov ds, ax
	mov es, ax
	mov fs, ax
	mov gs, ax
	mov ss, ax

	; do a long jump to load CS with correct value
	jmp 0x08:.load_cs
.load_cs:
	ret

; ----- Interrupts -----

; gets called for ALL interrupts
isr_common:
	; push registers to match struct TrapFrame (in reverse order)
	pushad
	push ds
	push es
	push fs
	push gs

	; load kernel data segment
	push ebx
	mov bx, 0x10
	mov ds, bx
	mov es, bx
	mov fs, bx
	mov gs, bx
	pop ebx

	extern handle_interrupt
	call handle_interrupt

	pop gs
	pop fs
	pop es
	pop ds
	popad
	add esp, 8      ; pop error code and interrupt number
	iret            ; pops (CS, EIP, EFLAGS) and also (SS, ESP) if privilege change occurs

; generate isr stubs that jump to isr_common, in order to get a consistent stack frame

%macro ISR_ERROR_CODE 1
global isr%1
isr%1:
	push dword %1   ; interrupt number
	jmp isr_common
%endmacro

%macro ISR_NO_ERROR_CODE 1
global isr%1
isr%1:
	push dword 0    ; dummy error code to align with TrapFrame
	push dword %1   ; interrupt number
	jmp isr_common
%endmacro

; exceptions and CPU reserved interrupts 0 - 31
ISR_NO_ERROR_CODE 0
ISR_NO_ERROR_CODE 1
ISR_NO_ERROR_CODE 2
ISR_NO_ERROR_CODE 3
ISR_NO_ERROR_CODE 4
ISR_NO_ERROR_CODE 5
ISR_NO_ERROR_CODE 6
ISR_NO_ERROR_CODE 7
ISR_ERROR_CODE    8
ISR_NO_ERROR_CODE 9
ISR_ERROR_CODE    10
ISR_ERROR_CODE    11
ISR_ERROR_CODE    12
ISR_ERROR_CODE    13
ISR_ERROR_CODE    14
ISR_NO_ERROR_CODE 15
ISR_NO_ERROR_CODE 16
ISR_NO_ERROR_CODE 17
ISR_NO_ERROR_CODE 18
ISR_NO_ERROR_CODE 19
ISR_NO_ERROR_CODE 20
ISR_NO_ERROR_CODE 21
ISR_NO_ERROR_CODE 22
ISR_NO_ERROR_CODE 23
ISR_NO_ERROR_CODE 24
ISR_NO_ERROR_CODE 25
ISR_NO_ERROR_CODE 26
ISR_NO_ERROR_CODE 27
ISR_NO_ERROR_CODE 28
ISR_NO_ERROR_CODE 29
ISR_NO_ERROR_CODE 30
ISR_NO_ERROR_CODE 31

; IRQs 0 - 15 are mapped to 32 - 47
ISR_NO_ERROR_CODE 32 ; PIT
ISR_NO_ERROR_CODE 33
ISR_NO_ERROR_CODE 34
ISR_NO_ERROR_CODE 35
ISR_NO_ERROR_CODE 36
ISR_NO_ERROR_CODE 37
ISR_NO_ERROR_CODE 38
ISR_NO_ERROR_CODE 39
ISR_NO_ERROR_CODE 40
ISR_NO_ERROR_CODE 41
ISR_NO_ERROR_CODE 42
ISR_NO_ERROR_CODE 43
ISR_NO_ERROR_CODE 44
ISR_NO_ERROR_CODE 45
ISR_NO_ERROR_CODE 46
ISR_NO_ERROR_CODE 47

; syscall 0x80
global isr128
ISR_NO_ERROR_CODE 128

global isr_redirect_table
isr_redirect_table:
%assign i 0
%rep 48
	dd isr%+i
%assign i i+1
%endrep

; ----- Tasks -----

; void switch_context(Task* from, Task* to);
; swaps stack pointer to next task's kernel stack
global switch_context
switch_context:
	mov eax, [esp + 4] ; eax = from
	mov edx, [esp + 8] ; edx = to
	
	; these are the callee-saved registers on x86 according to cdecl
	; they will change once we go off and execute the other task
	; so we need to preserve them for when we get back
	
	; think of it from the perspective of the calling function,
	; it wont notice that we go off and execute some other code
	; but when we return to it, suddenly the registers it
	; expected to remain unchanged has changed
	push ebx
	push esi
	push edi
	push ebp

	; swap kernel stack pointer and store them
	mov [eax + 4], esp ; from->kesp = esp
	mov esp, [edx + 4] ; esp = to->kesp
	
	; NewTaskKernelStack will match the stack from here on out.

	pop ebp
	pop edi
	pop esi
	pop ebx

	ret ; new tasks hijack the return address to new_task_setup

global new_task_setup
new_task_setup:
	; update the segment registers
	pop ebx
	mov ds, bx
	mov es, bx
	mov fs, bx
	mov gs, bx
	
	; zero out registers so they dont leak to userspace
	xor eax, eax
	xor ebx, ebx
	xor ecx, ecx
	xor edx, edx
	xor esi, esi
	xor edi, edi
	xor ebp, ebp

	; exit the interrupt, placing us in the real task entry function
	iret
