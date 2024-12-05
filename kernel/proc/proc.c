#include "lib/print.h"
#include "lib/str.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "proc/initcode.h"
#include "memlayout.h"

// in trampoline.S
extern char trampoline[];

// in swtch.S
extern void swtch(context_t* old, context_t* new);

// in trap_user.c
extern void trap_user_return();


// 第一个进程
static proc_t proczero;

// 获得一个初始化过的用户页表
// 完成了trapframe 和 trampoline 的映射
pgtbl_t proc_pgtbl_init(uint64 trapframe)
{
    pgtbl_t user_pgtbl = (pgtbl_t)pmem_alloc(false);

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
    
    // pid 设置
    proczero.pid = 1;
    // pagetable 初始化
    proczero.tf = (trapframe_t *)pmem_alloc(false);
    proczero.pgtbl = proc_pgtbl_init((uint64)proczero.tf);
    // ustack 映射 + 设置 ustack_pages 
    page = (uint64)pmem_alloc(false);
    vm_mappages(proczero.pgtbl, TRAPFRAME - PGSIZE, page, PGSIZE, PTE_R | PTE_W | PTE_U);
    proczero.ustack_pages = 1;
    // data + code 映射
    assert(initcode_len <= PGSIZE, "proc_make_first: initcode too big\n");
    page = (uint64)pmem_alloc(false);
    // 注意这里权限是PTE_R | PTE_W | PTE_X | PTE_U
    vm_mappages(proczero.pgtbl, USER_BASE, page, PGSIZE, PTE_R | PTE_W | PTE_X | PTE_U);
    memmove((void *)page, initcode, initcode_len);
    // 设置 heap_top
    proczero.heap_top = (PGSIZE << 1);
    // tf字段设置
    proczero.tf->epc = USER_BASE; // 用户程序计数器 用户代码的起始地址
    proczero.tf->sp = TRAPFRAME; // 用户栈指针 用户栈的起始地址
    // 内核字段设置
    proczero.kstack = KSTACK(1); // 分配内核栈给第一个进程
    proczero.ctx.ra = (uint64)trap_user_return; // 设置从用户态返回到内核态的返回地址ra为trap_user_return
    proczero.ctx.sp = KSTACK(1) + PGSIZE; // 设置内核上下文的栈指针sp为内核栈的顶部
    // 上下文切换
    cpu_t *c = mycpu();
    c->proc = &proczero;
    swtch(&c->ctx, &proczero.ctx); // 从当前内核线程切换到第一个用户态进程
}