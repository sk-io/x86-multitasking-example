#include "defs.h"

// ----- GDT / TSS -----

GDTEntry gdt_entries[NUM_GDT_ENTRIES];
GDTPointer gdt_pointer;
TSS tss;

void set_gdt_entry(uint32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t flags) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_mid    = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;
    
    gdt_entries[num].granularity |= flags & 0xF0;
    gdt_entries[num].access      = access;
}

void setup_gdt() {
    gdt_pointer.limit = NUM_GDT_ENTRIES * 8 - 1;
    gdt_pointer.base = (uint32_t) &gdt_entries;

    memset((uint8_t*) &tss, 0, sizeof(tss));
    tss.ss0 = GDT_KERNEL_DATA;

    set_gdt_entry(0, 0, 0, 0, 0);                // 0x00: null
    set_gdt_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xC0); // 0x08: kernel text
    set_gdt_entry(2, 0, 0xFFFFFFFF, 0x92, 0xC0); // 0x10: kernel data
    set_gdt_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xC0); // 0x18: User mode code segment
    set_gdt_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xC0); // 0x20: User mode data segment
    set_gdt_entry(5, (uint32_t) &tss, sizeof(tss), 0x89, 0x40); // 0x28: tss

    flush_gdt((uint32_t) &gdt_pointer);
    flush_tss();
}

// ----- Interrupts -----

IDTEntry idt[256] __attribute__((aligned(16)));
IDTPointer idt_pointer;

void set_idt_entry(uint8_t vector, void* isr, uint8_t attributes) {
    idt[vector].isr_low    = (uint32_t) isr & 0xFFFF;
    idt[vector].kernel_cs  = GDT_KERNEL_CODE;
    idt[vector].reserved   = 0;
    idt[vector].attributes = attributes;
    idt[vector].isr_high   = (uint32_t) isr >> 16;
}

void remap_pic() {
    // send commands to PIC to remap IRQs
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

extern void* isr_redirect_table[];
extern void isr128();

void setup_interrupts() {
    memset((uint8_t*) &idt, 0, sizeof(IDTEntry) * 256);

    idt_pointer.limit = sizeof(IDTEntry) * 256 - 1;
    idt_pointer.base  = (uint32_t) &idt;

    remap_pic();

    for (int i = 0; i < 48; i++) {
        set_idt_entry(i, isr_redirect_table[i], 0x8E);
    }
    set_idt_entry(0x80, isr128, 0xEE); // syscalls have DPL 3

    asm volatile ("lidt %0" :: "m"(idt_pointer));
}

void handle_interrupt(TrapFrame regs) {
    *((uint16_t*) 0xB8000 + regs.interrupt) = 0xF100 | 'G';

    if (regs.interrupt >= 32 && regs.interrupt <= 47) {
        // acknowledge IRQ
        if (regs.interrupt >= 40) {
            outb(0xA0, 0x20); // slave PIC
        }
        outb(0x20, 0x20);

        if (regs.interrupt == 32 && timer_enabled) {
            timer_tick();
        }
    }

    if (regs.interrupt == 0x80) {
        // syscall

        *((uint16_t*) 0xB8000 + 80 * 2 + regs.eax) = 0xB000 | 'S';
    }
}

// ----- Timer -----

bool timer_enabled = false;

void setup_timer(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;

    outb(0x43, 0x36);

    uint8_t l = (uint8_t) (divisor & 0xFF);
    uint8_t h = (uint8_t) (divisor >> 8 & 0xFF);

    outb(0x40, l);
    outb(0x40, h);
}

void timer_tick() {
    schedule();
}

// ----- Tasks -----

Task tasks[MAX_TASKS];
int num_tasks;
Task* current_task;

void create_task(uint32_t id, uint32_t eip, uint32_t user_stack, uint32_t kernel_stack, bool kernel_task) {
    num_tasks++;
    
    // setup initial kernel stack

    uint8_t* kesp = (uint8_t*) kernel_stack;
    
    kesp -= sizeof(TrapFrame);
    TrapFrame* trap = (TrapFrame*) kesp;
    memset((uint8_t*) trap, 0, sizeof(TrapFrame));

    uint32_t code_selector = kernel_task ? GDT_KERNEL_CODE : (GDT_USER_CODE | DPL_USER);
    uint32_t data_selector = kernel_task ? GDT_KERNEL_DATA : (GDT_USER_DATA | DPL_USER);

    trap->cs = code_selector;
    trap->ds = data_selector;

    trap->usermode_ss = data_selector;
    trap->usermode_esp = user_stack;

    trap->eflags = 0x200; // enable interrupts
    trap->eip = eip;

    kesp -= sizeof(TaskReturnContext);
    TaskReturnContext* context = (TaskReturnContext*) kesp;
    context->edi = 0;
    context->esi = 0;
    context->ebx = 0;
    context->ebp = 0;
    context->eip = (uint32_t) isr_exit;

    tasks[id].kesp0 = kernel_stack;
    tasks[id].kesp = (uint32_t) kesp;
    tasks[id].id = id;
}

void setup_tasks() {
    memset((uint8_t*) tasks, 0, sizeof(Task) * MAX_TASKS);

    num_tasks = 1;
    current_task = &tasks[0];
    current_task->id = 0;

    // task 0 represents the execution we're in right now
}

void schedule() {
    // naive scheduling: just cycle through all the tasks
    int next_id = (current_task->id + 1) % num_tasks;

    Task* next = &tasks[next_id];
    Task* old = current_task;
    current_task = next;

    // update tss
    tss.esp0 = next->kesp0;

    // switch context, may not return here
    switch_context(old, next);
}

void task1() {
    // do a software interrupt
    asm volatile(
        "mov $3, %eax \n"
        "int $0x80"
    );

    // there's no memory protection so we can write directly to vga buffer
    // (just to show that it's still running)
    uint8_t a = 0;
    while (true) *((uint16_t*) 0xB8000 + 495) = 0xF200 | a++;
}

void task2() {
    asm volatile(
        "mov $5, %eax \n"
        "int $0x80"
    );

    uint8_t a = 0;
    while (true) *((uint16_t*) 0xB8000 + 496) = 0xF300 | a++;
}

void task3() {
    asm volatile(
        "mov $7, %eax \n"
        "int $0x80"
    );

    uint8_t a = 0;
    while (true) *((uint16_t*) 0xB8000 + 497) = 0xF400 | a++;
}

void kernel_main() {
    setup_gdt();

    // clear screen
    memset((uint8_t*) 0xB8000, 0, 80 * 25 * sizeof(uint16_t));

    setup_interrupts();
    setup_timer(1000);
    setup_tasks();

    create_task(1, (uint32_t) task1, 0xC80000, 0xC00000, false);
    create_task(2, (uint32_t) task2, 0xD80000, 0xD00000, false);
    create_task(3, (uint32_t) task3, 0xE80000, 0xE00000, false);

    timer_enabled = true;
    enable_interrupts();

    // kernel / idle thread
    uint8_t a = 0;
    while (true) {
        *((uint16_t*) 0xB8000 + 494) = 0xFA00 | a++;
        halt();
    }
}

// ----- Utils -----

void* memset(uint8_t* dest, uint8_t val, uint32_t len) {
    uint8_t* temp = (uint8_t*) dest;
    for (; len != 0; len--) *temp++ = val;
    return dest;
}
