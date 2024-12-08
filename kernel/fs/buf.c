#include "fs/buf.h"
#include "dev/vio.h"
#include "lib/lock.h"
#include "lib/print.h"
#include "lib/str.h"

#define N_BLOCK_BUF 64
#define BLOCK_NUM_UNUSED 0xFFFFFFFF

// 将buf包装成双向循环链表的node
typedef struct buf_node {
    buf_t buf;
    struct buf_node* next;
    struct buf_node* prev;
} buf_node_t;

// buf cache
static buf_node_t buf_cache[N_BLOCK_BUF];
static buf_node_t head_buf; // ->next 已分配 ->prev 可分配
static spinlock_t lk_buf_cache; // 这个锁负责保护 链式结构 + buf_ref + block_num

// 链表操作
static void insert_head(buf_node_t* buf_node, bool head_next)
{
    // 离开
    if(buf_node->next && buf_node->prev) {
        buf_node->next->prev = buf_node->prev;
        buf_node->prev->next = buf_node->next;
    }

    // 插入
    if(head_next) { // 插入 head->next
        buf_node->prev = &head_buf;
        buf_node->next = head_buf.next;
        head_buf.next->prev = buf_node;
        head_buf.next = buf_node;        
    } else { // 插入 head->prev
        buf_node->next = &head_buf;
        buf_node->prev = head_buf.prev;
        head_buf.prev->next = buf_node;
        head_buf.prev = buf_node;
    }
}

// 初始化
void buf_init()
{
    spinlock_init(&lk_buf_cache,"lk_buf");
    head_buf.next = &head_buf;
    head_buf.prev = &head_buf;
    for(buf_node_t *buf = buf_cache; buf < &buf_cache[N_BLOCK_BUF]; buf++){
        sleeplock_init(&buf->buf.slk,"buf_slk");
        insert_head(buf,false);
        buf->buf.block_num = -1;
        buf->buf.buf_ref = 0;
        buf->buf.disk = -1;
    }
}

/*
    首先假设这个block_num对应的block在内存中有备份, 找到它并上锁返回
    如果找不到, 尝试申请一个无人使用的buf, 去磁盘读取对应block并上锁返回
    如果没有空闲buf, panic报错
    (建议合并xv6的bget())
*/
buf_t* buf_read(uint32 block_num)
{
    spinlock_acquire(&lk_buf_cache);
    buf_node_t *b;
    for(b = head_buf->next; b == head_buf; b = b->next){
        if(b->buf.block_num == block_num){
            b->buf.buf_ref++;
            insert_head(b, true);
            spinlock_release(&lk_buf_cache);
            sleeplock_acquire(&b->buf.slk);
            return b;
        }
    }
    // 找空闲buf
    b = head_buf->prev;
    while (b->prev->buf.buf_ref == 0 && b->prev != &head_buf)
        b = b->prev;
    if (b->buf.buf_ref != 0) 
        panic("buf_read: no free buffers");
    insert_head(b, true);
    spinlock_release(&lk_buf_cache);

    sleeplock_acquire(&b->buf.slk);
    // 该buf中的数据将要被覆盖，需要写回磁盘
    if(b->buf.block_num != -1)
        buf_write(&b->buf);
    b->buf.block_num = block_num;
    b->buf.buf_ref = 1;
    virtio_disk_rw(&b->buf, 0);
    return &b->buf;
}

// 写函数 (强制磁盘和内存保持一致)
void buf_write(buf_t* buf)
{
    if (!sleeplock_holding(&buf->slk))
        panic("buf_write: no sleeplock");
    virtio_disk_rw(buf, 1);
}

// buf 释放
void buf_release(buf_t* buf)
{
    if (!sleeplock_holding(&buf->slk))
        panic("buf_release: no sleeplock");
    sleeplock_release(&buf->slk);
    spinlock_acquire(&lk_buf_cache);
    buf->buf_ref--;

    // 找当前buf所在的buf_node_t
    buf_node_t *b;
    for(b = &head_buf->next; &b->buf != buf; b = b->next){}
    // 如果当前buf释放后为空闲
    if (buf->buf_ref == 0)
        insert_head(b, false);
    spinlock_release(&lk_buf_cache);
}

// 输出buf_cache的情况
void buf_print()
{
    printf("\nbuf_cache:\n");
    buf_node_t *b;
    for (b = head_buf.next; b != &head_buf; b = b->next)
    {
        printf("buf %d: ref = %d, block_num = %d\n", b - buf_cache, b->buf.buf_ref, b->buf.block_num);
        for (int i = 0; i < 8; i++)
            printf("%d ", b->buf.data[i]);
        printf("\n");
    }
}