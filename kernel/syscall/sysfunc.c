#include "proc/cpu.h"
#include "mem/vmem.h"
#include "mem/pmem.h"
#include "mem/mmap.h"
#include "lib/str.h"
#include "lib/print.h"
#include "syscall/sysfunc.h"
#include "syscall/syscall.h"

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
        printf("look: heap_top = %p\n", old_heap_top);
    // 堆扩展
    }else if(old_heap_top < new_heap_top){
        p->heap_top = uvm_heap_grow(p->pgtbl,old_heap_top,new_heap_top-old_heap_top);
        printf("grow: heap_top = %p\n", p->heap_top);
    // 堆收缩
    }else{
        p->heap_top = uvm_heap_ungrow(p->pgtbl,old_heap_top,old_heap_top-new_heap_top);
        printf("ungrow: heap_top = %p\n", p->heap_top);
    }
    vm_print(p->pgtbl);
    printf("\n");
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
    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");
    return start;
}

// 取消内存映射
// uint64 start 起始地址
// uint32 len   范围(字节, 检查是否是page-aligned)
// 成功返回0 失败返回-1
uint64 sys_munmap()
{
    proc_t* p = myproc();
    uint64 start;
    uint32 len;
    arg_uint64(0, &start);
    arg_uint32(1, &len);

    // 检查是否是page-aligned
    if(len % PGSIZE != 0) return -1;
    uint32 npages = len / PGSIZE;
    uvm_munmap(start, npages);

    uvm_show_mmaplist(p->mmap);
    vm_print(p->pgtbl);
    printf("\n");
    return 0;
}

// copyin 测试 (int 数组)
// uint64 addr
// uint32 len
// 返回 0
uint64 sys_copyin()
{
    proc_t* p = myproc();
    uint64 addr;
    uint32 len;

    arg_uint64(0, &addr);
    arg_uint32(1, &len);

    int tmp;
    for(int i = 0; i < len; i++) {
        uvm_copyin(p->pgtbl, (uint64)&tmp, addr + i * sizeof(int), sizeof(int));
        printf("get a number from user: %d\n", tmp);
    }

    return 0;
}

// copyout 测试 (int 数组)
// uint64 addr
// 返回数组元素数量
uint64 sys_copyout()
{
    int L[5] = {1, 2, 3, 4, 5};
    proc_t* p = myproc();
    uint64 addr;

    arg_uint64(0, &addr);
    uvm_copyout(p->pgtbl, addr, (uint64)L, sizeof(int) * 5);

    return 5;
}

// copyinstr测试
// uint64 addr
// 成功返回0
uint64 sys_copyinstr()
{
    char s[64];

    arg_str(0, s, 64);
    printf("get str from user: %s\n", s);

    return 0;
}

uint64 sys_copy()
{
    pgtbl_t new = (pgtbl_t)pmem_alloc(false);
    proc_t* p = myproc();
    uvm_copy_pgtbl(p->pgtbl, new, p->heap_top, p->ustack_pages, p->mmap);
    printf("the old pagetable:\n");
    vm_print(p->pgtbl);
    printf("\n");
    printf("the new pagetable:\n");
    vm_print(new);
    printf("\n");
    return 0;
}

uint64 sys_destroy_pgtbl()
{
    proc_t *p = myproc();
    printf("the pagetable of the current process:\n");
    vm_print(p->pgtbl);
    printf("\n");
    uvm_destroy_pgtbl(p->pgtbl);
    printf("after destroying the pagetable of the current process:\n");
    vm_print(p->pgtbl);
    printf("\n");
    return 0;
}