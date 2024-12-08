#include "fs/buf.h"
#include "fs/fs.h"
#include "fs/bitmap.h"
#include "lib/print.h"
#include "lib/str.h"

extern super_block_t sb;

// search and set bit
// 根据bitmap找一块空闲的块
static uint32 bitmap_search_and_set(uint32 bitmap_block)
{
    buf_t *buf = buf_read(bitmap_block);
    uint8 m; // m表示掩码
    for(uint32 i = 0; i < sb.block_size * 8; i++){
        m = 1 << (i % 8);
        // 该位代表的块是空闲的
        if((buf->data[i / 8] & m) == 0){
            buf->data[i / 8] |= m; // 将该位设为1 代表的块不是空闲的
            buf_release(buf);
            return bitmap_block + i + 1; // 返回盘块号
        }
    }
    panic("bitmap_search_and_set: no free block");
    return -1;
}

// unset bit
static void bitmap_unset(uint32 bitmap_block, uint32 num)
{
    buf_t *bp = buf_read(bitmap_block);
    num -= (bitmap_block + 1);
    // 创建目标位为1的掩码
    uint8 m = 1 << (num % 8);
    // 只有m位的1被清除，其余保持不变
    bp->data[num / 8] &= ~m;
    // 释放缓冲区
    buf_release(bp);
}

// 分配空闲的磁盘块，返回分配的磁盘块号，失败返回-1
uint32 bitmap_alloc_block()
{
    uint32 block_num = bitmap_search_and_set(sb.data_bitmap_start);
    if (block_num != -1)
    {
        buf_t *bp = buf_read(block_num);
        // 把这个磁盘块清零
        memset(bp->data, 0, BLOCK_SIZE);
        buf_release(bp);
        return block_num;
    }
    return -1;
}

void bitmap_free_block(uint32 block_num)
{
    bitmap_unset(sb.data_bitmap_start, block_num);
}

uint32 bitmap_alloc_inode()
{
    uint32 inode_num = bitmap_search_and_set(sb.inode_bitmap_start);
    if (inode_num != -1)
    {
        buf_t *bp = buf_read(inode_num);
        // 把这个磁盘块清零
        memset(bp->data, 0, BLOCK_SIZE);
        buf_release(bp);
        return inode_num;
    }
    return -1;
}

void bitmap_free_inode(uint32 inode_num)
{
    bitmap_unset(sb.inode_bitmap_start, inode_num);
}

// 打印所有已经分配出去的bit序号(序号从0开始)
// for debug
void bitmap_print(uint32 bitmap_block_num)
{
    buf_t *bp = buf_read(bitmap_block_num);
    uint32 bi;
    printf("bitmap:\n");
    for (bi = 0; bi < sb.block_size * 8; bi++)
    {
        uint8 m = 1 << (bi % 8);
        if ((bp->data[bi/8]&m)!=0)
            printf("bit %d is allocated\n", bi);
    }
    buf_release(bp);
    printf("over\n");
    printf("\n");
}