#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"
#include "dev/timer.h"

// 堆伸缩
// uint64 new_heap_top 新的堆顶 (如果是0代表查询, 返回旧的堆顶)
// 成功返回新的堆顶 失败返回-1
uint64 sys_brk()
{
    uint64 new_heap_top;
    proc_t* p = myproc();
    uint64 old_heap_top = p->heap_top;
    arg_uint64(0, &new_heap_top);
    // 查询
    if(new_heap_top == 0){
        // printf("look: heap_top = %p\n", old_heap_top);
    // 堆扩展
    }else if(old_heap_top < new_heap_top){
        p->heap_top = uvm_heap_grow(p->pgtbl,old_heap_top,new_heap_top-old_heap_top);
        // printf("grow: heap_top = %p\n", p->heap_top);
    // 堆收缩
    }else{
        p->heap_top = uvm_heap_ungrow(p->pgtbl,old_heap_top,old_heap_top-new_heap_top);
        // printf("ungrow: heap_top = %p\n", p->heap_top);
    }
    // vm_print(p->pgtbl);
    // printf("\n");
    return p->heap_top;
}

// 内存映射
// uint64 start 起始地址 (如果为0则由内核自主选择一个合适的起点, 通常是顺序扫描找到一个够大的空闲空间)
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回映射空间的起始地址, 失败返回-1
uint64 sys_mmap()
{
    proc_t* p = myproc();
    uint64 start;
    uint32 len;
    arg_uint64(0, &start);
    arg_uint32(1, &len);

    // 检查是否是page-aligned
    if(len % PGSIZE != 0) return -1;
    uint32 npages = len / PGSIZE;
    // 内核自主选择一个合适的起点
    if(start == 0){
        mmap_region_t *mmap;
        // 顺序扫描找到一个够大的空闲空间
        for (mmap = p->mmap; mmap && mmap->npages < npages; mmap = mmap->next){}
        if(mmap->npages >= npages) uvm_mmap(mmap->begin, npages, PTE_W | PTE_R | PTE_U);
        else return -1;
    }else{
        uvm_mmap(start, npages, PTE_W | PTE_R | PTE_U);
    }
    // uvm_show_mmaplist(p->mmap);
    // vm_print(p->pgtbl);
    // printf("\n");
    return start;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    // proc_t* p = myproc();
    uint64 start;
    uint32 len;
    arg_uint64(0, &start);
    arg_uint32(1, &len);

    // 检查是否是page-aligned
    if(len % PGSIZE != 0) return -1;
    uint32 npages = len / PGSIZE;
    uvm_munmap(start, npages);

    // uvm_show_mmaplist(p->mmap);
    // vm_print(p->pgtbl);
    // printf("\n");
    return 0;
}

// 打印字符
// uint64 addr
uint64 sys_print()
{
    char addr[30];
    arg_str(0, (char *)addr, 30);
    printf("%s", addr);
    return 0;
}

// 进程复制
uint64 sys_fork()
{
    return proc_fork();
}

// 进程等待
// uint64 addr  子进程退出时的exit_state需要放到这里 
uint64 sys_wait()
{
    uint64 addr;
    arg_uint64(0, &addr);
    proc_wait(addr);
    return 0;
}

// 进程退出
// int exit_state
uint64 sys_exit()
{
    uint64 exit_state;
    arg_uint64(0, &exit_state);
    proc_exit(exit_state);
    return 0;
}

extern timer_t sys_timer;

// 进程睡眠一段时间
// uint32 second 睡眠时间
// 成功返回0, 失败返回-1
uint64 sys_sleep()
{
    uint32 second;
    uint32 ticks0;
    arg_uint32(0, &second);
    spinlock_acquire(&sys_timer.lk);
    ticks0 = sys_timer.ticks;
    while (sys_timer.ticks - ticks0 < second)
        proc_sleep(&sys_timer.ticks, &sys_timer.lk);
    spinlock_release(&sys_timer.lk);
    return 0;
}