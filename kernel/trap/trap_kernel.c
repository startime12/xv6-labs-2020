#include "lib/print.h"
#include "dev/timer.h"
#include "dev/uart.h"
#include "dev/plic.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "memlayout.h"
#include "riscv.h"

// 中断信息
char* interrupt_info[16] = {
    "U-mode software interrupt",      // 0
    "S-mode software interrupt",      // 1
    "reserved-1",                     // 2
    "M-mode software interrupt",      // 3
    "U-mode timer interrupt",         // 4
    "S-mode timer interrupt",         // 5
    "reserved-2",                     // 6
    "M-mode timer interrupt",         // 7
    "U-mode external interrupt",      // 8
    "S-mode external interrupt",      // 9
    "reserved-3",                     // 10
    "M-mode external interrupt",      // 11
    "reserved-4",                     // 12
    "reserved-5",                     // 13
    "reserved-6",                     // 14
    "reserved-7",                     // 15
};

// 异常信息
char* exception_info[16] = {
    "Instruction address misaligned", // 0
    "Instruction access fault",       // 1
    "Illegal instruction",            // 2
    "Breakpoint",                     // 3
    "Load address misaligned",        // 4
    "Load access fault",              // 5
    "Store/AMO address misaligned",   // 6
    "Store/AMO access fault",         // 7
    "Environment call from U-mode",   // 8
    "Environment call from S-mode",   // 9
    "reserved-1",                     // 10
    "Environment call from M-mode",   // 11
    "Instruction page fault",         // 12
    "Load page fault",                // 13
    "reserved-2",                     // 14
    "Store/AMO page fault",           // 15
};

// in trap.S
// 内核中断处理流程
extern void kernel_vector();

// 初始化trap中全局共享的东西
void trap_kernel_init()
{
    plic_init();
    timer_create();
}

// 各个核心trap初始化
void trap_kernel_inithart()
{
    plic_inithart();
    // 将内核中断处理流程的入口地址写入 stvec 寄存器 (supervisor模式)
    w_stvec((uint64)kernel_vector);
}

// 外设中断处理 (基于PLIC)
void external_interrupt_handler()
{
    // 获取中断号
    int irq = plic_claim();
    if(irq == UART_IRQ) uart_intr();
    else if (irq == VIRTIO_IRQ) virtio_disk_intr();
    else if(irq) printf("unexpected interrupt irq=%d\n", irq);
    // 中断已完成
    if(irq) plic_complete(irq);
}

// 时钟中断处理 (基于CLINT)
void timer_interrupt_handler()
{
    // 为什么这里不用 获取中断号？因为中断号是基于plic的
    if(mycpuid() == 0){
        // 系统时钟的更新只需要单个CPU去做即可
        timer_update();
        // printf("cpu %d: di da\n",r_tp());
        // printf("ticks = %d\n",timer_get_ticks());
    }

    // 清除 sip 寄存器中的SSIP位（第1位）来确认软件中断
    w_sip(r_sip() & ~2);

    // 强制进程交出CPU使用权
    if(myproc() != 0 && myproc()->state == RUNNING)
        proc_yield();
}

// 在kernel_vector()里面调用
// 内核态trap处理的核心逻辑
void trap_kernel_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

    // 确认trap来自S-mode且此时trap处于关闭状态
    assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
    assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

    int trap_id = scause & 0xf; 

    // 中断异常处理核心逻辑
    if(scause & 0x8000000000000000L){
        switch (trap_id)
        {
        case 1: // 时钟中断
            timer_interrupt_handler();
            break;
        case 9: // 外部中断
            external_interrupt_handler();
            break;
        default: // 其他中断
            printf("%s\n",interrupt_info[trap_id]);
            break;
        }
    }else{
        // 异常
        printf("%s\n",exception_info[trap_id]);
        printf("scause %p\n", scause);
        printf("stval %p\n", stval);
        printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
        panic("kerneltrap");
    }

    // 后面会用到
    w_sepc(sepc);
    w_sstatus(sstatus);
}