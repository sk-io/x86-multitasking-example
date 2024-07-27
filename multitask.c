#include "multitask.h"

// ----- GDT / TSS -----

GDTEntry gdt_entries[NUM_GDT_ENTRIES];
GDTPointer gdt_pointer;
TSS tss;

void set_gdt_entry(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
	gdt_entries[num].base_low    = base & 0xFFFF;
	gdt_entries[num].base_mid    = base >> 16 & 0xFF;
	gdt_entries[num].base_high   = base >> 24 & 0xFF;
	gdt_entries[num].limit_low   = limit & 0xFFFF;
	gdt_entries[num].granularity = (flags & 0xF0) | (limit >> 16 & 0xF);
	gdt_entries[num].access      = access;
}

void setup_gdt() {
	gdt_pointer.limit = NUM_GDT_ENTRIES * 8 - 1;
	gdt_pointer.base = (uint32_t) &gdt_entries;

	memset((uint8_t*) &tss, 0, sizeof(tss));
	tss.ss0 = GDT_KERNEL_DATA;

	set_gdt_entry(0, 0, 0, 0, 0);                // 0x00: null
	set_gdt_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xC0); // 0x08: kernel mode text
	set_gdt_entry(2, 0, 0xFFFFFFFF, 0x92, 0xC0); // 0x10: kernel mode data
	set_gdt_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xC0); // 0x18: user mode code segment
	set_gdt_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xC0); // 0x20: user mode data segment
	set_gdt_entry(5, (uint32_t) &tss, sizeof(tss), 0x89, 0x40); // 0x28: tss

	load_gdt((uint32_t) &gdt_pointer);
	// load task register with the TSS segment selector 0x28
	asm("ltr %%ax" :: "a"((uint16_t) GDT_TSS));
}

// ----- Interrupts -----

IDTEntry idt[256] __attribute__((aligned(16)));
IDTPointer idt_pointer;

void set_idt_entry(uint8_t vector, void* isr, uint8_t attributes) {
	idt[vector].isr_low    = (uint32_t) isr & 0xFFFF;
	idt[vector].segment_selector = GDT_KERNEL_CODE; // we only want to run interrupts in kernel mode
	idt[vector].reserved   = 0;
	idt[vector].attributes = attributes;
	idt[vector].isr_high   = (uint32_t) isr >> 16;
}

// send commands to PIC to remap IRQs from 0-15 to 32-47
// so as not to interfere with CPU exception interrupts.
void remap_pic() {
	outb(0x20, 0x11);
	outb(0xA0, 0x11);
	outb(0x21, 0x20);
	outb(0xA1, 0x28);
	outb(0x21, 0x04);
	outb(0xA1, 0x02);
	outb(0x21, 0x01);
	outb(0xA1, 0x01);
	outb(0x21, 0x00);
	outb(0xA1, 0x00);
}

// defined in multitask.asm
extern void* isr_redirect_table[];
extern void isr128();

void setup_interrupts() {
	remap_pic();

	// clear IDT
	memset((uint8_t*) &idt, 0, sizeof(IDTEntry) * 256);

	// all our gate types are "32-bit Interrupt Gates"
	// meaning interrupts are disabled when we enter the handler

	for (int i = 0; i < 48; i++) {
		set_idt_entry(i, isr_redirect_table[i], 0x8E);
	}
	set_idt_entry(0x80, isr128, 0xEE); // syscalls have DPL 3

	// pass IDT to the CPU
	idt_pointer.limit = sizeof(IDTEntry) * 256 - 1;
	idt_pointer.base  = (uint32_t) &idt;
	asm("lidt %0" :: "m"(idt_pointer));
}

// interrupts are disabled in here
// so we can't get interrupted whilst servicing another interrupt
// unless we enable them again, which we might want to do
// for lengthy interrupts like file IO syscalls etc.
// so we don't hog the CPU
void handle_interrupt(TrapFrame regs) {
	// print some garbage to the screen
	*(VGA_MEMORY + 80 + regs.interrupt) = 0xF100 | 'G';

	// check if its a IRQ
	if (regs.interrupt >= 32 && regs.interrupt <= 47) {
		// acknowledge IRQ
		if (regs.interrupt >= 40) {
			outb(0xA0, 0x20); // slave PIC
		}
		outb(0x20, 0x20);

		// check if this is a PIT interrupt
		if (regs.interrupt == 32) {
			schedule();
		}
	}
 
	if (regs.interrupt == 0x80) {
		// syscall demonstration

		*(VGA_MEMORY + 80 * 2 + regs.eax) = 0xB000 | 'S';
	}
}

// ----- PIT -----

void setup_pit(uint32_t frequency) {
	uint32_t divisor = 1193180 / frequency;

	outb(0x43, 0x36);

	uint8_t l = (uint8_t) (divisor & 0xFF);
	uint8_t h = (uint8_t) (divisor >> 8 & 0xFF);

	outb(0x40, l);
	outb(0x40, h);
}

// ----- Tasks -----

Task tasks[MAX_TASKS];
int num_tasks;
Task* current_task;

void create_task(uint32_t id, uint32_t eip, uint32_t user_stack, uint32_t kernel_stack, bool kernel_task) {
	num_tasks++;

	// we can pass things to the task by pushing to its user stack
	// with cdecl, this will pass it as arguments
	user_stack -= 4;
	*(uint32_t*) user_stack = id; // first arg to task func
	user_stack -= 4;
	*(uint32_t*) user_stack = 0; // task func return address, shouldnt be used

	uint32_t code_selector = kernel_task ? GDT_KERNEL_CODE : (GDT_USER_CODE | RPL_USER);
	uint32_t data_selector = kernel_task ? GDT_KERNEL_DATA : (GDT_USER_DATA | RPL_USER);

	uint8_t* kesp = (uint8_t*) kernel_stack;

	// we need to set up the initial kernel stack for this task
	// this stack will be loaded next time switch_context gets
	// called for this task
	// once switch_context switches esp to this stack, the ret
	// instruction will pop off a return value, so we redirect it
	// to new_task_setup to init registers and exit the interrupt
	kesp -= sizeof(NewTaskKernelStack);
	NewTaskKernelStack* stack = (NewTaskKernelStack*) kesp;
	stack->ebp = stack->edi = stack->esi = stack->ebx = 0;
	stack->switch_context_return_addr = (uint32_t) new_task_setup;
	stack->data_selector = data_selector;
	stack->eip = eip;
	stack->cs = code_selector;
	stack->eflags = 0x200; // enable interrupts
	stack->usermode_esp = user_stack;
	stack->usermode_ss = data_selector;

	tasks[id].kesp_bottom = kernel_stack;
	tasks[id].kesp = (uint32_t) kesp;
	tasks[id].id = id;
}

void setup_tasks() {
	memset((uint8_t*) tasks, 0, sizeof(Task) * MAX_TASKS);

	// task 0 represents the execution we're in right now
	// (the kernel/idle thread)
	num_tasks = 1;
	current_task = &tasks[0];
	current_task->id = 0;
}

void schedule() {
	// naive scheduling: just cycle through all the tasks
	int next_id = (current_task->id + 1) % num_tasks;

	Task* next = &tasks[next_id];
	Task* old = current_task;
	current_task = next;

	// update tss, esp will be set to this when an interrupt happens
	// (only when going from user to kernel though)
	tss.esp0 = next->kesp_bottom;

	// switch context, doesn't return here for newly created tasks
	switch_context(old, next);
}

static void task_entry(uint32_t id) {
	// do a software interrupt
	asm("int $0x80" :: "a"(id));

	// there's no memory protection so we can write directly to vga buffer
	// (just to show that it's still running)
	uint8_t a = 0;
	while (true) *(VGA_MEMORY + id) = 0x0A00 | a++;

	// IMPORTANT: all tasks need to end in an infinite loop, otherwise
	// the cpu will just continue executing garbage code from here
}

void kernel_main() {
	setup_gdt();

	// clear screen
	memset((uint8_t*) 0xB8000, 0, 80 * 25 * sizeof(uint16_t));

	setup_interrupts();
	setup_pit(1000);
	setup_tasks();

	// since theres no allocator, we'll just manually designate
	// some stack space for the tasks
	create_task(1, (uint32_t) task_entry, 0xC80000, 0xC00000, false);
	create_task(2, (uint32_t) task_entry, 0xD80000, 0xD00000, false);
	create_task(3, (uint32_t) task_entry, 0xE80000, 0xE00000, false);

	enable_interrupts();

	// kernel / idle thread
	uint8_t a = 0;
	while (true) {
		*(VGA_MEMORY) = 0x0900 | a++;
		halt();
	}
}

// ----- Utils -----

void* memset(uint8_t* dest, uint8_t val, uint32_t len) {
	uint8_t* temp = (uint8_t*) dest;
	for (; len != 0; len--) *temp++ = val;
	return dest;
}
