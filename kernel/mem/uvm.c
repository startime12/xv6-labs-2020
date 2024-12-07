#include "mem/mmap.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "proc/cpu.h"
#include "lib/print.h"
#include "lib/str.h"
#include "memlayout.h"

// 连续虚拟空间的复制(在uvm_copy_pgtbl中使用)
static void copy_range(pgtbl_t old, pgtbl_t new, uint64 begin, uint64 end)
{
    uint64 va, pa, page;
    int flags;
    pte_t* pte;

    for(va = begin; va < end; va += PGSIZE)
    {
        pte = vm_getpte(old, va, false);
        assert(pte != NULL, "uvm_copy_pgtbl: pte == NULL");
        assert((*pte) & PTE_V, "uvm_copy_pgtbl: pte not valid");
        
        pa = (uint64)PTE_TO_PA(*pte);
        flags = (int)PTE_FLAGS(*pte);

        // 这里申请的是用户页表要对应的物理页
        page = (uint64)pmem_alloc(false);
        memmove((char*)page, (const char*)pa, PGSIZE);
        vm_mappages(new, va, page, PGSIZE, flags);
    }
}

// 两个 mmap_region 区域合并
// 保留一个 释放一个 不操作 next 指针
// 在uvm_munmap里使用
static void mmap_merge(mmap_region_t* mmap_1, mmap_region_t* mmap_2, bool keep_mmap_1)
{
    // 确保有效和紧临
    assert(mmap_1 != NULL && mmap_2 != NULL, "mmap_merge: NULL");
    assert(mmap_1->begin + mmap_1->npages * PGSIZE == mmap_2->begin, "mmap_merge: check fail");
    
    // merge
    if(keep_mmap_1) {
        mmap_1->npages += mmap_2->npages;
        mmap_region_free(mmap_2);
    } else {
        mmap_2->begin -= mmap_1->npages * PGSIZE;
        mmap_2->npages += mmap_1->npages;
        mmap_region_free(mmap_1);
    }
}

// 打印以 mmap 为首的 mmap 链
// for debug
void uvm_show_mmaplist(mmap_region_t* mmap)
{
    mmap_region_t* tmp = mmap;
    printf("\nmmap allocable area:\n");
    if(tmp == NULL)
        printf("NULL\n");
    while(tmp != NULL) {
        printf("allocable region: %p ~ %p\n", tmp->begin, tmp->begin + tmp->npages * PGSIZE);
        tmp = tmp->next;
    }
}

// 递归释放 页表占用的物理页 和 页表管理的物理页
// ps: 顶级页表level = 3, level = 0 说明是页表管理的物理页
static void destroy_pgtbl(pgtbl_t pgtbl, uint32 level)
{
    pte_t pte;
    pgtbl_t child;
    assert(level<=3,"uvm_destroy_pgtbl: level > 3");
    if(level==0)
        goto free;
    for (int i = 0; i < PGSIZE / sizeof(pte);i++)
    {
        pte = pgtbl[i];
        if(pte & PTE_V)
        {
            if ((level>1)&&(!PTE_CHECK(pte)))
            {
                printf("pte = %p\n", pte);
                panic("uvm_destroy_pgtbl: pte fail");
            }
            child = (pgtbl_t)PTE_TO_PA(pte);
            destroy_pgtbl(child, level - 1);
            pgtbl[i] = 0;
        }
    }
    free:
        if (level>0)
            pmem_free((uint64)pgtbl, true);
        else
            pmem_free((uint64)pgtbl, false);
}

// 页表销毁：trapframe 和 trampoline 单独处理
void uvm_destroy_pgtbl(pgtbl_t pgtbl)
{
    // trapframe 和 trampoline页不会被释放，因为它被映射到内核的虚拟地址空间中，并且是内核运行所必需的
    vm_unmappages(pgtbl, TRAPFRAME, PGSIZE, false);
    vm_unmappages(pgtbl, TRAMPOLINE, PGSIZE, false);
    destroy_pgtbl(pgtbl, 3);
}

// 拷贝页表 (拷贝并不包括trapframe 和 trampoline)
void uvm_copy_pgtbl(pgtbl_t old, pgtbl_t new, uint64 heap_top, uint32 ustack_pages, mmap_region_t* mmap)
{
    /* step-1: USER_BASE ~ heap_top */
    copy_range(old, new, USER_BASE, heap_top);
    /* step-2: ustack */
    copy_range(old, new, TRAPFRAME - ustack_pages * PGSIZE, TRAPFRAME);
    /* step-3: mmap_region */
    uint64 alloc = MMAP_BEGIN; // 映射区域起点
    for (mmap_region_t *region = mmap; region != NULL; region = region->next)
    {
        while (alloc < region->begin)
        {
            copy_range(old, new, alloc, alloc + PGSIZE);
            alloc += PGSIZE;
        }
    }
}

// 在用户页表和进程mmap链里 新增mmap区域 [begin, begin + npages * PGSIZE)
// 页面权限为perm
void uvm_mmap(uint64 begin, uint32 npages, int perm)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_mmap: begin not aligned");

    // 修改 mmap 链 (分情况的链式操作)
    proc_t* p = myproc();
    mmap_region_t *mmap = p->mmap;
    uint64 ostart,nstart;
    uint64 oend,nend;
    nstart = begin;
    nend = begin + npages * PGSIZE;
    mmap_region_t *pmmap = NULL;
    while (true)
    {
        ostart = mmap->begin;
        oend = mmap->begin + mmap->npages * PGSIZE;
        if(ostart < nstart && nstart <= oend){
            // ostart nstart nend oend
            if(nend < oend){
                mmap_region_t *nmap = mmap_region_alloc();
                nmap->next = mmap->next;
                mmap->next = nmap;
                mmap->npages = (nstart - ostart) / PGSIZE;
                nmap->begin = nend;
                nmap->npages = (oend - nend) / PGSIZE;
            // ostart nstart nend(oend)
            }else if(nend == oend){
                mmap->npages = (nstart - ostart) / PGSIZE;
            }else panic("uvm_mmap: npages too big");
            break;
        }else if(ostart == nstart){
            // nstart(ostart) nend oend
            if(nend < oend){
                mmap->begin = nend;
                mmap->npages = (oend - nend) / PGSIZE;
            // nstart(ostart) nend(oend)
            }else if (nend == oend){
                // 此时不是第一个mmap
                if(pmmap) pmmap->next = mmap->next;
                // 此时是第一个mmap
                else p->mmap = mmap->next;
                mmap_region_free(mmap);
            }
            else panic("uvm_mmap: npages too big");
            break;
        }
        pmmap = mmap;
        mmap = mmap->next;
    }
    
    // 修改页表 (物理页申请 + 页表映射)
    for(uint64 va = begin; va < (begin + npages * PGSIZE); va += PGSIZE)
        vm_mappages(p->pgtbl, va, (uint64)pmem_alloc(false), PGSIZE, perm);
}

// 在用户页表和进程mmap链里释放mmap区域 [begin, begin + npages * PGSIZE)
void uvm_munmap(uint64 begin, uint32 npages)
{
    if(npages == 0) return;
    assert(begin % PGSIZE == 0, "uvm_munmap: begin not aligned");

    // new mmap_region 的产生
    mmap_region_t *nmmap = mmap_region_alloc();
    nmmap->begin = begin;
    nmmap->npages = npages;
    // 尝试合并 mmap_region
    proc_t* p = myproc();
    mmap_region_t *mmap = p->mmap;
    mmap_region_t *pmmap = NULL;
    uint64 end = begin + npages * PGSIZE;
    // 没有到达当前进程的最后一个mmap
    while(!mmap){
        // 刚好在当前mmap前面
        if(end == mmap->begin){
            // mmap1 mmap2 保留后面的，不需要修改next指针
            mmap_merge(nmmap, mmap, false);
        // 在当前mmap前面，但是与当前mmap合并不了，并且在上一次循环中比较过，与前一个mmap也合并不了
        }else if(end < mmap->begin){
            if(pmmap){
                nmmap->next = pmmap->next;
                pmmap->next = nmmap;
            }else{
                nmmap->next = p->mmap;
                p->mmap = nmmap;
            }
        // 刚好在当前mmap后面
        }else if(begin == (mmap->begin + mmap->npages * PGSIZE)){
            // mmap1 mmap2 保留前面的，不需要修改next指针
            mmap_merge(mmap, nmmap, true);
        // 在当前mmap后面，但是与当前mmap合并不了，并且后面已经没有mmap了
        }else if(!mmap->next){
            mmap->next = nmmap;
            nmmap->next = NULL;
        }
        pmmap = mmap;
        mmap = mmap->next;
    }
    // 页表释放
    for(uint64 va = begin; va < (begin + npages * PGSIZE); va += PGSIZE)
        vm_unmappages(p->pgtbl, va, PGSIZE, true);
}

// 用户堆空间增加, 返回新的堆顶地址 (注意栈顶最大值限制)
// 在这里无需修正 p->heap_top
uint64 uvm_heap_grow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    if(len < 0) return heap_top;
    // heap_top = ALIGN_UP(heap_top, PGSIZE);
    uint64 new_heap_top = heap_top + len;
    if(new_heap_top > TRAPFRAME - PGSIZE){
        // 栈顶超过最大限制
        panic("heap grow too large");
    }
    for(int i = heap_top; i < new_heap_top; i += PGSIZE){
        // 因为 pmem_alloc 一次只能申请一页，所以需要使用循环
        // 用户堆空间增加，增加的部分需要做地址映射
        vm_mappages(pgtbl, i, (uint64)pmem_alloc(false), PGSIZE, PTE_W | PTE_R | PTE_U);
    }

    return new_heap_top;
}

// 用户堆空间减少, 返回新的堆顶地址
// 在这里无需修正 p->heap_top
uint64 uvm_heap_ungrow(pgtbl_t pgtbl, uint64 heap_top, uint32 len)
{
    if(len < 0) return heap_top;
    uint64 new_heap_top = heap_top - len;
    if(ALIGN_UP(new_heap_top, PGSIZE) < ALIGN_UP(heap_top, PGSIZE)){
        int npages = (ALIGN_UP(heap_top, PGSIZE) - ALIGN_UP(new_heap_top, PGSIZE)) / PGSIZE;
        vm_unmappages(pgtbl, ALIGN_UP(new_heap_top, PGSIZE), npages * PGSIZE, true);
    }

    return new_heap_top;
}

// 用户态地址空间[src, src+len) 拷贝至 内核态地址空间[dst, dst+len)
// 注意: src dst 不一定是 page-aligned
void uvm_copyin(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    // n是当前这页可以拷贝的字节数，len是还需要拷贝的字节数
    uint64 n, va0, pa0;
    while(len > 0)
    {
        va0 = ALIGN_DOWN(src, PGSIZE);
        pte_t *p = vm_getpte(pgtbl, va0, false);
        pa0 = PTE_TO_PA(*p);
        if(pa0 == 0)
            return;
        n = PGSIZE - (src - va0);
        if(n > len)
        n = len;
        // 内核地址是直接映射，所以不需要查找内核地址
        memmove((void*)dst, (void *)(pa0 + (src - va0)), n);
        len -= n;
        dst += n;
        src = va0 + PGSIZE;
    }

}

// 内核态地址空间[src, src+len） 拷贝至 用户态地址空间[dst, dst+len)
void uvm_copyout(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 len)
{
    uint64 n, va0, pa0;
    while(len > 0)
    {
        va0 = ALIGN_DOWN(dst, PGSIZE);
        pte_t *p = vm_getpte(pgtbl, va0, 0);
        pa0 = PTE_TO_PA(*p);
        if (pa0 == 0)
            return;
        n = PGSIZE - (dst - va0);
        if (n > len)
            n = len;
        memmove((void *)(pa0 + (dst - va0)), (void *)src, n);
        len -= n;
        src += n;
        dst = va0 + PGSIZE;
    }
}

// 用户态字符串拷贝到内核态
// 最多拷贝maxlen字节, 中途遇到'\0'则终止
// 注意: src dst 不一定是 page-aligned
void uvm_copyin_str(pgtbl_t pgtbl, uint64 dst, uint64 src, uint32 maxlen)
{
    uint64 n, va0, pa0;
    int got_null = 0;
    while(got_null == 0 && maxlen > 0)
    {
        va0 = ALIGN_DOWN(src, PGSIZE);
        pte_t *pte = vm_getpte(pgtbl, va0, 0);
        pa0 = PTE_TO_PA(*pte);
        if (pa0 == 0)
            return;
        n = PGSIZE - (src - va0);
        if(n > maxlen)
            n = maxlen;
        char *p = (char *) (pa0 + (src - va0));
        while(n > 0)
        {
            if(*p == '\0')
            {
                *(char*)dst = '\0';
                got_null = 1;
                break;
            } 
            else
                *(char*)dst = *p;
            --n;
            --maxlen;
            p++;
            dst++;
        }
        src = va0 + PGSIZE;
    }
}