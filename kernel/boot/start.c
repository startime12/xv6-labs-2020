#include "riscv.h"

__attribute__ ((aligned (16))) uint8 CPU_stack[4096 * NCPU]; // 每个CPU的栈是4096个字节

void main();
void timerinit();

void start()
{
    // MPP : Machine Mode -> Supervisor Mode
    uint64 mstatus = r_mhartid(); // 获取mstatus寄存器的值
    mstatus &= ~MSTATUS_MPP_MASK; // 将mpp位清零
    mstatus |= MSTATUS_MPP_S;     // 将mpp位设置为Supervisor Mode
    w_mstatus(mstatus);           // 将设置好的值写入mstatus寄存器

    // 将 mepc 设置为 main 函数的地址
    // 表示切换到 S 态时执行 main 函数的代码
    w_mepc((uint64)main);         // main 是一个函数指针，指向程序中的 main 函数

    // 关闭页表,即关闭虚拟地址转换功能
    w_satp(0);                    

    // 把所有中断和异常委托给S-mode                              
    w_medeleg(0xffff);
    w_mideleg(0xffff);    
    // 打开中断,控制哪些中断可以在 Supervisor 模式下被启用或禁用
    w_sie(r_sie() | SIE_SEIE | SIE_STIE | SIE_SSIE);  

    // 设置时钟中断
    timerinit();

    // 将hart_id存储到tp寄存器中
    int mhartid = r_mhartid();
    w_tp(mhartid);

    // 切换到监督者模式，并跳转到main函数
    asm volatile("mret");
}

void timerinit(){}