// kernel virtual memory management

#include "mem/pmem.h"
#include "mem/vmem.h"
#include "lib/print.h"
#include "lib/str.h"
#include "riscv.h"
#include "memlayout.h"

extern char trampoline[]; // in trampoline.S

pgtbl_t kernel_pgtbl; // 内核页表

// 根据pagetable,找到va对应的pte
// 若设置alloc=true 则在PTE无效时尝试申请一个物理页
// 成功返回PTE, 失败返回NULL
// 提示：使用 VA_TO_VPN PTE_TO_PA PA_TO_PTE
pte_t* vm_getpte(pgtbl_t pgtbl, uint64 va, bool alloc)
{
    // pgtbl是一个unit64类型的指针，指向一个页面，这个页面里面有很多页表项
    if( va > VA_MAX)
        panic("vm_getpte");
    // Page Table Entry，页表项
    for(int i = 2; i > 0; i--){
        // 指针就是一个地址！
        // pte是地址，*pte是地址所在处的数字
        pte_t *pte = &pgtbl[VA_TO_VPN(va,i)];
        if(*pte & PTE_V){
            pgtbl = (pgtbl_t)PTE_TO_PA(*pte);
        }else{
            // pmem_alloc 返回一个 page_node_t *类型
            // 即返回一个指针，这个指针指向一个 page_node_t（一个指针 64位）
            // 这里的pmem_alloc实际上是构建三级页表的过程
            if(alloc && (pgtbl = (pgtbl_t)pmem_alloc(true)) != 0){
                memset(pgtbl, 0, PGSIZE);
                // 将pte地址所在处填充修改
                *pte = PA_TO_PTE(pgtbl) | PTE_V;
            }else return NULL;
        }
    }
    
    // 从 VPN[0]低级页表 中取地址
    return &pgtbl[VA_TO_VPN(va,0)];
}

// 在pgtbl中建立 [va, va + len) -> [pa, pa + len) 的映射
// 本质是找到va在页表对应位置的pte并修改它
// 检查: va pa 应当是 page-aligned, len(字节数) > 0, va + len <= VA_MAX
// 注意: perm 应该如何使用
void vm_mappages(pgtbl_t pgtbl, uint64 va, uint64 pa, uint64 len, int perm)
{
    uint64 start,end;
    pte_t* pte;
    if((va % PGSIZE) != 0) panic("vm_mappages: va not aligned");
    if((pa % PGSIZE) != 0) panic("vm_mappages: pa not aligned");
    
    // start = ALIGN_DOWN(va, PGSIZE);
    start = va;
    end = ALIGN_DOWN(va + len - 1, PGSIZE);
    if(!(len > 0) || !(va + len <= VA_MAX)) panic("vm_mappages: va and len not right");
    for( ; ; start+=PGSIZE, pa+=PGSIZE){
        pte = vm_getpte(pgtbl,start,1);
        if(pte == NULL) panic("vm_mappages: not find pte");
        *pte = PA_TO_PTE(pa) | perm | PTE_V;
        if(start == end) break;
    }
}

// 解除pgtbl中[va, va+len)区域的映射
// 如果freeit == true则释放对应物理页, 默认是用户的物理页
void vm_unmappages(pgtbl_t pgtbl, uint64 va, uint64 len, bool freeit)
{
    uint64 start,end;
    pte_t* pte;
    if((va % PGSIZE) != 0) panic("vm_unmappages: va not aligned");
    // start = page-aligned(va, PGSIZE); // 如果va是page-aligned，就不需要使用page-aligned
    start = va;
    end = va + len; // 这里千万不能使用ALIGN_DOWN
    if(!(len > 0) || !(va + len <= VA_MAX)){
        panic("vm_unmappages: va and len not right");
    }

    for( ; start < end; start += PGSIZE){
        pte = vm_getpte(pgtbl,start,0);
        if(pte == NULL) panic("vm_unmappages: not find pte");
        if((*pte & PTE_V) == 0) panic("vm_unmappages: not map");
        if(PTE_FLAGS(*pte) == PTE_V) panic("uvmunmap: not a leaf");
        if(freeit) pmem_free(PTE_TO_PA(*pte), false);
        *pte = 0;
    }
}

// 完成 UART CLINT PLIC 内核代码区 内核数据区 可分配区域 trampoline kstack 的映射
// 相当于填充kernel_pgtbl
void kvm_init()
{
    // 分配kernel_pgtbl，并将其初始化为0
    kernel_pgtbl = (pgtbl_t)pmem_alloc(true);
    memset(kernel_pgtbl, 0, PGSIZE);

    vm_mappages(kernel_pgtbl, UART_BASE, UART_BASE, PGSIZE, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, CLINT_BASE, CLINT_BASE, 0x10000, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, PLIC_BASE, PLIC_BASE, 0x400000, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, KERNEL_BASE, KERNEL_BASE, (uint64)KERNEL_DATA-KERNEL_BASE, PTE_R | PTE_X);
    vm_mappages(kernel_pgtbl, (uint64)KERNEL_DATA, (uint64)KERNEL_DATA, (uint64)ALLOC_BEGIN-(uint64)KERNEL_DATA, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, (uint64)ALLOC_BEGIN, (uint64)ALLOC_BEGIN, (uint64)ALLOC_END - (uint64)ALLOC_BEGIN, PTE_R | PTE_W);
    vm_mappages(kernel_pgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
    
    // 映射kstack，每个进程有自己的kstack，目前只有一个进程，只映射KSTACK(1)
    // 内核页表其他部分所有进程共用
    // vm_mappages(kernel_pgtbl, KSTACK(1), (uint64)pmem_alloc(true), PGSIZE, PTE_R | PTE_W);
}

// 使用新的页表，刷新TLB
void kvm_inithart()
{
    w_satp(MAKE_SATP(kernel_pgtbl));
    sfence_vma();//刷新TLB
}

// for debug
// 输出页表内容
void vm_print(pgtbl_t pgtbl)
{
    // 顶级页表，次级页表，低级页表
    pgtbl_t pgtbl_2 = pgtbl, pgtbl_1 = NULL, pgtbl_0 = NULL;
    pte_t pte;

    printf("level-2 pgtbl: pa = %p\n", pgtbl_2);
    for(int i = 0; i < PGSIZE / sizeof(pte_t); i++) 
    {
        pte = pgtbl_2[i];
        if(!((pte) & PTE_V)) continue;
        assert(PTE_CHECK(pte), "vm_print: pte check fail (1)");
        pgtbl_1 = (pgtbl_t)PTE_TO_PA(pte);
        printf(".. level-1 pgtbl %d: pa = %p\n", i, pgtbl_1);
        
        for(int j = 0; j < PGSIZE / sizeof(pte_t); j++)
        {
            pte = pgtbl_1[j];
            if(!((pte) & PTE_V)) continue;
            assert(PTE_CHECK(pte), "vm_print: pte check fail (2)");
            pgtbl_0 = (pgtbl_t)PTE_TO_PA(pte);
            printf(".. .. level-0 pgtbl %d: pa = %p\n", j, pgtbl_2);

            for(int k = 0; k < PGSIZE / sizeof(pte_t); k++) 
            {
                pte = pgtbl_0[k];
                if(!((pte) & PTE_V)) continue;
                assert(!PTE_CHECK(pte), "vm_print: pte check fail (3)");
                printf(".. .. .. physical page %d: pa = %p flags = %d\n", k, (uint64)PTE_TO_PA(pte), (int)PTE_FLAGS(pte));                
            }
        }
    }
}