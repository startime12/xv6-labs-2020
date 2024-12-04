#include "lib/lock.h"
#include "lib/print.h"
#include "dev/timer.h"
#include "memlayout.h"
#include "riscv.h"

/*-------------------- 工作在M-mode --------------------*/

// in trap.S M-mode时钟中断处理流程()
extern void timer_vector();

// 每个CPU在时钟中断中需要的临时空间(考虑为什么可以这么写)
static uint64 mscratch[NCPU][5];

// 时钟初始化
// called in start.c
void timer_init()
{   
    uint64 cpuid = r_mhartid();
    *(uint64*)CLINT_MTIMECMP(cpuid) = *(uint64*)CLINT_MTIME + INTERVAL;
    // scratch[0]、scratch[1]和scratch[2]用于保存a0、a1和a2寄存器的值
    // scratch[3]用于存储CLINT_MTIMECMP寄存器的地址
    // scratch[4]用于存储期望的时钟中断间隔（interval）
    mscratch[cpuid][3] = CLINT_MTIMECMP(cpuid);
    mscratch[cpuid][4] = INTERVAL;
    // 将scratch写入MSR寄存器 mscratch
    w_mscratch((uint64)mscratch[cpuid]);

    // mtvec寄存器 Machine Trap-Vector Base-Address Register，用于设置中断入口地址
    // 将时钟中断处理程序的入口地址写入 mtvec 寄存器 (机器模式)
    w_mtvec((uint64)timer_vector);

    // mstatus寄存器 控制cpu核当前的一些状态信息
    // 启用机器模式下的中断
    w_mstatus(r_mstatus() | MSTATUS_MIE);

    // 启用机器模式下的时钟中断
    w_mie(r_mie() | MIE_MTIE);
}


/*--------------------- 工作在S-mode --------------------*/

// 系统时钟
static timer_t sys_timer;

// 时钟创建(初始化系统时钟)
void timer_create()
{
    spinlock_init(&sys_timer.lk,"sys_timer");
    sys_timer.ticks = 0;
}

// 时钟更新(ticks++ with lock)
void timer_update()
{
    spinlock_acquire(&sys_timer.lk);
    sys_timer.ticks++;
    spinlock_release(&sys_timer.lk);
}

// 返回系统时钟ticks
uint64 timer_get_ticks()
{
    return sys_timer.ticks;
}