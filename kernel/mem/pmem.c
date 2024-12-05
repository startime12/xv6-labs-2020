#include "mem/pmem.h"
#include "lib/print.h"
#include "lib/lock.h"
#include "lib/str.h"

// 物理页节点
typedef struct page_node {
    struct page_node* next;
} page_node_t;

// 许多物理页构成一个可分配的区域
typedef struct alloc_region {
    uint64 begin;          // 起始物理地址
    uint64 end;            // 终止物理地址
    spinlock_t lk;         // 自旋锁(保护下面两个变量)
    uint32 allocable;      // 可分配页面数    
    page_node_t list_head; // 可分配链的链头节点
} alloc_region_t;

// 内核和用户可分配的物理页分开
static alloc_region_t kern_region, user_region;

#define KERN_PAGES 1024 // 内核可分配空间占1024个pages

// 物理内存初始化
void pmem_init(void)
{
    // 内核使用
    kern_region.begin = (uint64)ALLOC_BEGIN;
    kern_region.end = kern_region.begin + KERN_PAGES * PGSIZE;
    spinlock_init(&kern_region.lk,"kern_region");
    kern_region.allocable = KERN_PAGES;
    kern_region.list_head.next = NULL;
    freerange(kern_region.begin,kern_region.end,true);

    // 用户使用
    user_region.begin = kern_region.end;
    user_region.end = (uint64)ALLOC_END;
    spinlock_init(&user_region.lk,"user_region");
    user_region.allocable = ( user_region.end - user_region.begin ) / PGSIZE;
    user_region.list_head.next = NULL;
    freerange(user_region.begin,user_region.end,false);
}

// 返回一个可分配的干净物理页
// 失败则panic锁死
void* pmem_alloc(bool in_kernel)
{   
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    spinlock_acquire(&region->lk);
    page_node_t *page_node = region->list_head.next;
    if(page_node){
        region->list_head.next = page_node->next;
        region->allocable--;
        spinlock_release(&region->lk);
        memset((void *)page_node, 0, PGSIZE); // 干净的物理页
        return (void *)page_node;
    }else{
        panic("pmem_alloc error");
    }
    return NULL;
}

// 释放物理页
// 失败则panic锁死
void pmem_free(uint64 page, bool in_kernel)
{
    alloc_region_t *region = in_kernel ? &kern_region : &user_region;
    // 检查传入的页地址是否在分配区域的范围内
    if (page < region->begin || page >= region->end || (page % PGSIZE)!=0) {
        panic("pmem_free: page out of range");
    }
    memset((void*)page, 0, PGSIZE);

    page_node_t *page_node = (page_node_t *)page;
    spinlock_acquire(&region->lk);
    page_node->next = region->list_head.next;
    region->list_head.next = page_node;
    region->allocable++;
    spinlock_release(&region->lk);
}

void freerange(uint64 start,uint64 end,bool in_kernel)
{
    char* p;
    p=(char*)ALIGN_UP((uint64)start,PGSIZE);
    for(; p+PGSIZE<=(char*)end;p+=PGSIZE)
        pmem_free((uint64)p,in_kernel);
}