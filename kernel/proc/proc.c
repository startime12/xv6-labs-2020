#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"
#include "mem/mmap.h"
#include "riscv.h"
#include "fs/fs.h"


/*----------------外部空间------------------*/

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();


// in kvm.c
extern pgtbl_t kernel_pgtbl;

/*----------------本地变量------------------*/

// 进程数组
static proc_t procs[NPROC];

// 第一个进程
static proc_t *proczero;

// 全局的pid和保护它的锁 
static int global_pid = 1;
static spinlock_t lk_pid;

// 申请一个pid(锁保护)
static int alloc_pid()
{
    int tmp = 0;
    spinlock_acquire(&lk_pid);
    assert(global_pid >= 0, "alloc_pid: overflow");
    tmp = global_pid++;
    spinlock_release(&lk_pid);
    return tmp;
}

// 释放锁 + 调用 trap_user_return
static void fork_return()
{
    static int first = 1;
    // 由于调度器中上了锁，所以这里需要解锁
    proc_t* p = myproc();
    spinlock_release(&p->lk);
    if (first) {
        first = 0; // 只初始化一次
        fs_init();
    }
    trap_user_return();
}

// 返回一个未使用的进程空间
// 设置pid + 设置上下文中的ra和sp
// 申请tf和pgtbl使用的物理页
proc_t* proc_alloc()
{
    proc_t *p;
    for(int i = 0; i < NPROC; i++){
        p = &procs[i];
        spinlock_acquire(&p->lk);
        if(p->state == UNUSED){
            p->pid = alloc_pid(); // pid 设置
            p->tf = (trapframe_t *)pmem_alloc(false);
            p->pgtbl = proc_pgtbl_init((uint64)p->tf); // pagetable 初始化
            if(!p->pgtbl){
                proc_free(p); // 调用者需持有p->lk
                spinlock_release(&p->lk);
                return 0;
            }
            // 设置上下文
            memset(&p->ctx, 0, sizeof(p->ctx));
            p->ctx.ra = (uint64)fork_return;
            p->ctx.sp = p->kstack + PGSIZE;
            return p;
        }
        spinlock_release(&p->lk);
    }
    return 0;
}

// 释放一个进程空间
// 释放pgtbl的整个地址空间
// 释放mmap_region到仓库
// 设置其余各个字段为合适初始值
// tips: 调用者需持有p->lk
void proc_free(proc_t* p)
{
    if(p->tf) pmem_free((uint64)p->tf, false); 
    if(p->pgtbl) uvm_destroy_pgtbl(p->pgtbl);
    if(p->mmap) mmap_region_free(p->mmap);   
    p->tf = 0;
    p->pgtbl = 0;
    p->pid = 0;
    p->parent = 0;
    p->exit_state = 0;
    p->state = UNUSED;
}

// 进程模块初始化
void proc_init()
{
    spinlock_init(&lk_pid,"lk_pid");
    proc_t *p;
    for(int i = 0; i < NPROC; i++){
        p = &procs[i];
        spinlock_init(&p->lk,"p->lk");
        // 设置kstack字段
        vm_mappages(kernel_pgtbl, KSTACK(i), (uint64)pmem_alloc(true), PGSIZE, PTE_R | PTE_W);
        p->kstack = KSTACK(i);
        spinlock_acquire(&p->lk);
        proc_free(p);
        spinlock_release(&p->lk);
    }
    // 内核页表修改后，刷新TLB
    kvm_inithart();
}

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    // 用户页表也在内核可分配的物理页里面，不在用户可分配的物理页里面
    // 只不过最低级的用户页表，即0级页表，即物理页，在用户可分配的物理页里面
    pgtbl_t user_pgtbl = (pgtbl_t)pmem_alloc(true);

    vm_mappages(user_pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    vm_mappages(user_pgtbl, TRAPFRAME, trapframe, PGSIZE, PTE_R | PTE_W );

    return user_pgtbl;
}

/*
    第一个用户态进程的创建
    它的代码和数据位于initcode.h的initcode数组

    第一个进程的用户地址空间布局:
    trapoline   (1 page)
    trapframe   (1 page)
    ustack      (1 page)
    .......
                        <--heap_top
    code + data (1 page)
    empty space (1 page) 最低的4096字节 不分配物理页，同时不可访问
*/
void proc_make_first()
{
    uint64 page;
    
    proczero = proc_alloc();
    if(proczero == 0) panic("proc_make_first: proczero == 0");
    // ustack 映射 + 设置 ustack_pages 
    page = (uint64)pmem_alloc(false);
    vm_mappages(proczero->pgtbl, TRAPFRAME - PGSIZE, page, PGSIZE, PTE_R | PTE_W | PTE_U);
    proczero->ustack_pages = 1;
    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    page = (uint64)pmem_alloc(false);
    // 注意这里权限是PTE_R | PTE_W | PTE_X | PTE_U
    vm_mappages(proczero->pgtbl, USER_BASE, page, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    memmove((void *)page, initcode, initcode_len);
    // 设置 heap_top
    proczero->heap_top = USER_BASE + PGSIZE;
    // 设置 mmap_region_t
    proczero->mmap = mmap_region_alloc();
    proczero->mmap->begin = MMAP_BEGIN;
    proczero->mmap->npages = (MMAP_END - MMAP_BEGIN) / PGSIZE;
    proczero->mmap->next = NULL;
    // tf字段设置
    proczero->tf->epc = USER_BASE; // 用户程序计数器 用户代码的起始地址
    proczero->tf->sp = TRAPFRAME; // 用户栈指针 用户栈的起始地址
    // 当前cpu使用此进程
    cpu_t *c = mycpu();
    c->proc = proczero;
    // 设置proczero状态为RUNNABLE
    proczero->state = RUNNABLE;
    // proc_alloc()时上的锁解锁
    spinlock_release(&proczero->lk);
}

// 进程复制
// UNUSED -> RUNNABLE
int proc_fork()
{
    proc_t *p = myproc();
    proc_t *newp = proc_alloc();
    if(newp == 0) panic("proc_fork: newp == 0");
    // newp->ctx // proc_alloc中设置
    // newp->exit_state
    newp->heap_top = p->heap_top;
    // newp->kstack // proc_init中设置
    // newp->lk // proc_init中设置
    newp->mmap = mmap_region_alloc();
    newp->mmap->begin = MMAP_BEGIN;
    newp->mmap->npages = (MMAP_END - MMAP_BEGIN) / PGSIZE;
    newp->mmap->next = NULL;
    newp->parent = p;
    uvm_copy_pgtbl(p->pgtbl, newp->pgtbl, newp->heap_top, 1, p->mmap);
    // newp->pid // proc_alloc -> alloc_pid 中设置
    newp->state = RUNNABLE;
    *(newp->tf) = *(p->tf);
    // 区分父子进程 子进程
    newp->tf->a0 = 0;
    
    // proc_alloc()时上的锁解锁
    spinlock_release(&newp->lk);
    // 区分父子进程 父进程
    return newp->pid;
}

// 进程放弃CPU的控制权
// RUNNING -> RUNNABLE
void proc_yield()
{
    struct proc *p = myproc();
    spinlock_acquire(&p->lk);
    p->state = RUNNABLE;
    proc_sched();
    spinlock_release(&p->lk);
}

// 等待一个子进程进入 ZOMBIE 状态
// 将退出的子进程的exit_state放入用户给的地址 addr
// 成功返回子进程pid，失败返回-1
int proc_wait(uint64 addr)
{
    proc_t *newp;
    int havechild;// 记录当前进程是否拥有子进程
    int pid;
    proc_t *p = myproc();
    spinlock_acquire(&p->lk);
    // 扫描全局进程组寻找已退出的子进程
    for(;;)
    {
        havechild = 0;
        // 遍历整个进程表
        for(newp = procs; newp < &procs[NPROC]; newp++)
        {
            // 是子进程
            if(newp->parent == p)
            {
                // 保证子进程并非处于exit()或swtch()函数中,spinlock_acquire(&newp->lk)使得父子进程同步
                spinlock_acquire(&newp->lk);
                havechild = 1;
                // 找到退出的子进程
                if(newp->state == ZOMBIE)
                {
                    pid = newp->pid;
                    // 将退出的子进程的退出状态放入用户给的地址addr
                    uvm_copyout(p->pgtbl, addr, (uint64)&newp->exit_state, sizeof(newp->exit_state));
                    // 释放子进程的资源
                    proc_free(newp);
                    // 释放已经持有的锁
                    spinlock_release(&newp->lk);
                    return pid;
                }
                spinlock_release(&newp->lk);
            }
        }
        // 没有子进程
        if(!havechild)
            return -1;
        // proc_yield();
        proc_sleep(p, &p->lk);
    }
}

// 父进程退出，子进程认proczero做父，因为它永不退出
// tips: 调用者需持有parent->lk
static void proc_reparent(proc_t* parent)
{
    proc_t *p;
    // 扫描全局进程组寻找子进程
    for(p = procs; p < &procs[NPROC]; p++)
    {
        // 是子进程
        if(p->parent == parent)
        {
            spinlock_acquire(&p->lk);
            p->parent = proczero;
            spinlock_release(&p->lk);
        }
    }
}

// 唤醒一个进程
// tips: 调用者需持有p->lk
static void proc_wakeup_one(proc_t* p)
{
    assert(spinlock_holding(&p->lk), "proc_wakeup_one: lock");
    if(p->state == SLEEPING && p->sleep_space == p) {
        p->state = RUNNABLE;
    }
}

// 进程退出
void proc_exit(int exit_state)
{
    proc_t *p = myproc();
    // 1号进程应该一直存在
    if(p == proczero)
        panic("proczero exiting");
    
    // 唤醒proczero进程
    spinlock_acquire(&proczero->lk);
    proc_wakeup_one(proczero);
    spinlock_release(&proczero->lk);

    // 如果原先的父进程退出，p->parent会变成proczero，原先父进程无法解锁，故先获取p->parent副本
    spinlock_acquire(&p->lk);
    proc_t *parent = p->parent;
    spinlock_release(&p->lk);

    // 在 wait() 中唤醒父进程需要父进程的锁
    spinlock_acquire(&parent->lk);

    // 将当前进程的子进程交给proczero监管
    spinlock_acquire(&p->lk);
    proc_reparent(p);
    // 让当前进程的父进程回收这个退出进程
    proc_wakeup_one(p->parent);
    p->exit_state = exit_state;
    p->state = ZOMBIE;
    spinlock_release(&parent->lk);
    proc_sched();
    panic("zombie exit");
}

// 进程切换到调度器
// ps: 调用者保证持有当前进程的锁
void proc_sched()
{
    int origin;
    proc_t *p = myproc();
    cpu_t *c = mycpu();

    // 检查是否持有进程 p 的锁
    if(!spinlock_holding(&p->lk)) panic("sched p->lock");
    // 检查当前 CPU 是否没有关闭核间中断（核间中断应该被关闭）
    if(c->noff != 1) panic("sched locks");
    // 检查当前进程的状态是否为 RUNNING
    if(p->state == RUNNING) panic("sched running");
    // 获取当前中断状态（是否允许中断）
    if(intr_get()) panic("sched interruptible");

    // 保存当前 CPU 的中断状态，以便之后恢复
    origin = c->origin;
    swtch(&p->ctx, &c->ctx);
    c->origin = origin;
}

// 调度器
void proc_scheduler()
{
    proc_t *p;
    cpu_t *c = mycpu();
  
    c->proc = 0;
    for(;;){
        // 开中断
        intr_on();
    
        for(p = procs; p < &procs[NPROC]; p++) {
            spinlock_acquire(&p->lk);
            if(p->state == RUNNABLE) {
                printf("proc %d is running\n", p->pid);
                p->state = RUNNING;
                c->proc = p; // 设置当前CPU的进程指针为选中的进程
                swtch(&c->ctx, &p->ctx);
                c->proc = 0; // 进程运行完成后，将当前CPU的进程指针重置为0，表示当前运行在内核态
            }
            spinlock_release(&p->lk);
        }
    }
}

// 进程睡眠在sleep_space
void proc_sleep(void* sleep_space, spinlock_t* lk)
{
    proc_t *p = myproc();
    
    if(lk != &p->lk){
        spinlock_acquire(&p->lk);
        spinlock_release(lk);
    }

    p->sleep_space = sleep_space;
    p->state = SLEEPING;

    // 当前进程放弃CPU控制权，并允许调度器选择另一个进程来运行
    proc_sched();

    p->sleep_space = 0;

    if(lk != &p->lk){
        spinlock_release(&p->lk);
        spinlock_acquire(lk);
    }
}

// 唤醒所有在sleep_space沉睡的进程
void proc_wakeup(void* sleep_space)
{
    proc_t *p;
    for(p = procs; p < &procs[NPROC]; p++) 
    {
        spinlock_acquire(&p->lk);
        if(p->state == SLEEPING && p->sleep_space == sleep_space)
            p->state = RUNNABLE;
        spinlock_release(&p->lk);
    }
}