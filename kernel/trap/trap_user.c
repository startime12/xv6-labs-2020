#include "lib/print.h"
#include "trap/trap.h"
#include "proc/cpu.h"
#include "mem/vmem.h"
#include "memlayout.h"
#include "riscv.h"

// in trampoline.S
extern char trampoline[];      // 内核和用户切换的代码
extern char user_vector[];     // 用户触发trap进入内核
extern char user_return[];     // trap处理完毕返回用户

// in trap.S
extern char kernel_vector[];   // 内核态trap处理流程

// in trap_kernel.c
extern char* interrupt_info[16]; // 中断错误信息
extern char* exception_info[16]; // 异常错误信息

// 在user_vector()里面调用
// 用户态trap处理的核心逻辑
void trap_user_handler()
{
    uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
    uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
    uint64 scause = r_scause();      // 引发trap的原因
    uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)
    proc_t* p = myproc();

    // 确认trap来自U-mode
    assert((sstatus & SSTATUS_SPP) == 0, "trap_user_handler: not from u-mode");

    // 目前在内核态（在处理当前的系统调用、中断或异常期间）
    // 如果发生另一个中断，处理器会使用内核的中断处理程序（kernelvec 指向的代码）来处理新的中断，而不是返回到用户空间
    w_stvec((uint64)kernel_vector);

    // 保存发生陷阱的pc
    p->tf->epc = sepc; 
    
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
        // 系统调用
        if(trap_id == 8){
            p->tf->epc += 4;
            // 开中断
            intr_on();
            printf("get a syscall from proc %d\n", myproc()->pid);
        }else{
            // 其他异常
            printf("%s\n",exception_info[trap_id]);
            printf("scause %p\n", scause);
            printf("stval %p\n", stval);
            printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
            panic("kerneltrap");
        }
    }
    trap_user_return();
}

// 调用user_return()
// 内核态返回用户态
void trap_user_return()
{
    proc_t* p = myproc();
    // 关中断
    intr_off();
    // 发生中断后使用用户的中断处理程序
    w_stvec(TRAMPOLINE + (user_vector - trampoline));
    //设置 trapframe 中的字段，以便在下一次进程进入内核时能够恢复正确的内核态上下文。
    p->tf->kernel_satp = r_satp();         // 内核页表
    p->tf->kernel_sp = p->kstack + PGSIZE; // 进程的内核栈
    p->tf->kernel_trap = (uint64)trap_user_handler; // trap处理程序
    p->tf->kernel_hartid = r_tp();         // cpu id
    // 修改 sstatus 寄存器
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // SPP 位清零以设置为用户模式
    x |= SSTATUS_SPIE; // 启用用户模式下的中断 = intr_on()
    w_sstatus(x);
    // 将 sepc 寄存器设置为用户态程序计数器，指向用户程序被中断时的下一条指令。
    w_sepc(p->tf->epc);
    // 设置用户页表
    uint64 satp = MAKE_SATP(p->pgtbl);
    // 计算 trampoline.S 中 userret 部分的地址，并跳转到该地址执行。
    // 这个跳转操作会切换到用户页表，恢复用户态寄存器，并使用 sret 指令切换到用户模式。
    uint64 fn = TRAMPOLINE + (user_return - trampoline);
    ((void (*)(uint64,uint64))fn)(TRAPFRAME, satp);
}