#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NUM_GDT_ENTRIES 6
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

// "requested privilage level"
#define RPL_USER 3

// fixed number of tasks for simplicity
#define MAX_TASKS 16

#define VGA_MEMORY ((uint16_t*) 0xB8000)

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) GDTEntry;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) GDTPointer;

typedef struct {
    uint16_t previous_task, __previous_task_unused;
    uint32_t esp0;
    uint16_t ss0, __ss0_unused;
    uint32_t esp1;
    uint16_t ss1, __ss1_unused;
    uint32_t esp2;
    uint16_t ss2, __ss2_unused;
    uint32_t cr3;
    uint32_t eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint16_t es, __es_unused;
    uint16_t cs, __cs_unused;
    uint16_t ss, __ss_unused;
    uint16_t ds, __ds_unused;
    uint16_t fs, __fs_unused;
    uint16_t gs, __gs_unused;
    uint16_t ldt_selector, __ldt_sel_unused;
    uint16_t debug_flag, io_map;
} __attribute__((packed)) TSS; // https://wiki.osdev.org/TSS

typedef struct {
    uint16_t isr_low; // ISR(interrupt service routine) address
    uint16_t segment_selector; // what to load into CS register? + privilege level
    uint8_t  reserved;
    uint8_t  attributes; // bits 40-47: gate type, DPL(desired privilege level, ignored for hw ints), present bit
    uint16_t isr_high;
} __attribute__((packed)) IDTEntry; // 32bit, aka "Gate Descriptor"

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) IDTPointer;

// matches the stack of isr_common in multitask.asm
typedef struct {
    // pushed by us:
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // esp is ignored
    uint32_t interrupt, error;

    // pushed by the CPU:
    uint32_t eip, cs, eflags, usermode_esp, usermode_ss;
} TrapFrame;

typedef struct {
	// callee-saved regs
	uint32_t ebp, edi, esi, ebx;

	// popped by ret in switch_context
	uint32_t switch_context_return_addr;
	
	// popped by us in new_kernel_entry
	uint32_t data_selector;

	// popped by iret in new_kernel_entry
    uint32_t eip, cs, eflags, usermode_esp, usermode_ss;
} NewTaskKernelStack;

typedef struct {
    uint32_t id;

    // each task has its own kernel stack
    //  this stack gets loaded on interrupts
    //  when context switching between two tasks,
    //  this stack is used to store the state of the registers etc.

    // we could also have stored them here though

    // kernel stack pointer, updated when switching contexts
    //  to switch to this task, we load this into esp and pop the state
    uint32_t kesp;
    
    // bottom(highest address) of kernel stack
    //  esp gets set to this via the TSS when transitioning from user to kernel mode on interrupts,
    //  so we set it to the bottom of this task's kernel stack address to get an empty stack.
    //  this is only used in user tasks.
    uint32_t kesp_bottom;
} Task;

// in multitask.asm
void load_gdt(uint32_t addr);
void switch_context(Task* from, Task* to);
void isr_exit();
void new_task_setup();

// in multitask.c
extern TSS tss;
extern Task* current_task;
extern bool timer_enabled;

void handle_interrupt(TrapFrame regs);
void timer_tick();
void schedule();
void* memset(uint8_t* dest, uint8_t val, uint32_t len);

static inline void outb(uint16_t port, uint8_t value) {
    asm("outb %1, %0" :: "dN" (port), "a" (value));
}

#define halt() asm volatile("hlt")
#define enable_interrupts() asm volatile("sti")
#define disable_interrupts() asm volatile("cli")
