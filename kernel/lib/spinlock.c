#include "lib/lock.h"
#include "lib/print.h"
#include "proc/cpu.h"
#include "riscv.h"

// 带层数叠加的关中断
void push_off(void)
{
    // 获取当前中断状态（是否允许中断）
    int old = intr_get();
    // 禁用中断
    intr_off();
    // 如果关中断的深度为0
    if(mycpu()->noff == 0)
        // 记录第一次关中断前的状态
        mycpu()->origin = old;
    mycpu()->noff += 1;
}

// 带层数叠加的开中断
void pop_off(void)
{
    if(intr_get())
        // 允许中断
        panic("pop_off - interruptible");
    if(mycpu()->noff < 1)
        panic("pop_off");
    mycpu()->noff -= 1;
    if(mycpu()->noff == 0 && mycpu()->origin)
        // 启用中断
        intr_on();
}

// 是否持有自旋锁
// 中断应当是关闭的
bool spinlock_holding(spinlock_t *lk)
{ 
    return (lk->locked && (lk->cpuid == mycpuid()));
}

// 自选锁初始化
void spinlock_init(spinlock_t *lk, char *name)
{
    lk->name = name;
    lk->locked = 0;
    lk->cpuid = 0;
}

// 获取自选锁
void spinlock_acquire(spinlock_t *lk)
{    
    push_off();

    if(spinlock_holding(lk))
        panic("acquire");   // 自旋锁不应该被同一个 CPU 重复获取

    // 如果 lk->locked 当前是 0（表示锁未被占用），那么 __sync_lock_test_and_set 会将其设置为 1（表示锁被占用），并返回原来的值 0。
    // 如果 lk->locked 当前是 1（表示锁已经被占用），那么 __sync_lock_test_and_set 会返回 1，不会改变 lk->locked 的值。
    // 只有0->1才能表示此时占用锁成功
    while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
        ;

    // 实现内存屏障
    __sync_synchronize();

    lk->cpuid = mycpuid();
} 

// 释放自旋锁
void spinlock_release(spinlock_t *lk)
{
    if(!spinlock_holding(lk))
        panic("release");

    // 注意：释放自旋锁要将cpuid设为一个正常cpuid不会使用的数字
    lk->cpuid = -1;

    __sync_synchronize();

    __sync_lock_release(&lk->locked);

    pop_off();
}