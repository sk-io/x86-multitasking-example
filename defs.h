#pragma once

#include <stdint.h>
#include <stdbool.h>

#define NUM_GDT_ENTRIES 6
#define GDT_KERNEL_CODE 0x08
#define GDT_KERNEL_DATA 0x10
#define GDT_USER_CODE   0x18
#define GDT_USER_DATA   0x20
#define GDT_TSS         0x28

#define DPL_USER 3

#define MAX_TASKS 16

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
} __attribute__ ((packed)) TSS;

typedef struct {
    uint16_t isr_low;
    uint16_t kernel_cs;
    uint8_t  reserved;
    uint8_t  attributes;
    uint16_t isr_high;
} __attribute__((packed)) IDTEntry;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) IDTPointer;

// matches the stack of isr_common in kernel.asm
typedef struct {
    // pushed by us:
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; // esp is ignored
    uint32_t interrupt, error;

    // pushed by the CPU:
    uint32_t eip, cs, eflags, usermode_esp, usermode_ss;
} TrapFrame;

// matches the stack of switch_context in kernel.asm
typedef struct {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebx;
    uint32_t ebp;
    uint32_t eip;
} TaskReturnContext;

typedef struct {
    uint32_t id;

    // kernel stack
    uint32_t kesp;
    uint32_t kesp0; // start of kernel stack. copied to TSS, unused in kernel threads?

    // page directory
    uint32_t cr3;
} Task;

extern TSS tss;
extern Task* current_task;
extern bool timer_enabled;

// kernel.asm
void flush_gdt(uint32_t addr);
void flush_tss();
void switch_context(Task* old, Task* new);
void isr_exit();

// kernel.c
void handle_interrupt(TrapFrame regs);
void timer_tick();
void schedule();
void* memset(uint8_t* dest, uint8_t val, uint32_t len);

static inline void outb(uint16_t port, uint8_t value) {
    asm volatile ("outb %1, %0" :: "dN" (port), "a" (value));
}

#define halt() asm volatile("hlt")
#define enable_interrupts() asm volatile("sti")
#define disable_interrupts() asm volatile("cli")
