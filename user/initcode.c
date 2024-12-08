// 测试bitmap
#include "sys.h"
#include "type.h"

int main()
{
    uint32 block_num_1 = syscall(SYS_alloc_block);
    uint32 block_num_2 = syscall(SYS_alloc_block);
    uint32 block_num_3 = syscall(SYS_alloc_block);
    syscall(SYS_free_block, block_num_2);
    syscall(SYS_free_block, block_num_1);
    syscall(SYS_free_block, block_num_3);
    
    while(1);
    return 0;
}

// 测试buf_read,buf_write和buf_release
// #include "sys.h"
// #include "type.h"

// int main()
// {
//     char buf[128];
//     uint64 buf_in_kernel[10];

//     // 初始状态:读了sb并释放了buf
//     syscall(SYS_print, "\nstate-1:");
//     syscall(SYS_show_buf);
    
//     // 耗尽所有 buf
//     for(int i = 0; i < 6; i++) {
//         buf_in_kernel[i] = syscall(SYS_read_block, 100 + i, buf);
//         buf[i] = 0xFF;
//         syscall(SYS_write_block, buf_in_kernel[i], buf);
//     }
//     syscall(SYS_print, "\nstate-2:");
//     syscall(SYS_show_buf);

//     // 释放两个buf-4 和 buf-1，查看链的状态
//     syscall(SYS_release_block, buf_in_kernel[3]);
//     syscall(SYS_release_block, buf_in_kernel[0]);
//     syscall(SYS_print, "\nstate-3:");
//     syscall(SYS_show_buf);

//     // 申请buf,测试LRU是否生效 + 测试103号block的lazy write
//     buf_in_kernel[6] = syscall(SYS_read_block, 106, buf);
//     buf_in_kernel[7] = syscall(SYS_read_block, 103, buf);
//     syscall(SYS_print, "\nstate-4:");
//     syscall(SYS_show_buf);

//     // 释放所有buf
//     syscall(SYS_release_block, buf_in_kernel[7]);
//     syscall(SYS_release_block, buf_in_kernel[6]);
//     syscall(SYS_release_block, buf_in_kernel[5]);
//     syscall(SYS_release_block, buf_in_kernel[4]);
//     syscall(SYS_release_block, buf_in_kernel[2]);
//     syscall(SYS_release_block, buf_in_kernel[1]);
//     syscall(SYS_print, "\nstate-5:");
//     syscall(SYS_show_buf);

//     while(1);
//     return 0;
// }