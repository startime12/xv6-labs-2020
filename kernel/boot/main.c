#include "riscv.h"
#include "lib/print.h"
#include "lib/lock.h"

// 在计算机体系结构中，缓存一致性是指最终存储在多个本地缓存中的共享数据的一致性。当多个客户端都维护同一个内存资源的缓存时，就可能出现数据不一致的问题。这种情况在多CPU并行系统中尤其常见。
volatile static int started = 0;

volatile static int sum = 0; // cpu 1 report: sum = 1013713 cpu 0 report: sum = 1362458
// static int sum = 0; // cpu 1 report: sum = 1000000 cpu 0 report: sum = 1000000
// int sum = 0; // cpu 1 report: sum = 1000000 cpu 0 report: sum = 1000000

// 锁的粒度指的是锁定代码块的大小。
// 锁的粒度越细，锁定的代码越少，系统的并发度越高，但同时需要更频繁地获取和释放锁，这可能会增加锁的争用。
// 锁的粒度越粗，锁定的代码越多，系统的并发度越低，但可以减少锁争用，减少上下文切换和调度开销。

int main()
{
    int cpuid = r_tp();
    if(cpuid == 0) {
        print_init();
        printf("cpu %d is booting!\n", cpuid);      
        __sync_synchronize();
        started = 1;
        for(int i = 0; i < 1000000; i++)
            __sync_fetch_and_add(&sum, 1);
            // sum++;
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    } else {
        while(started == 0);
        __sync_synchronize();
        printf("cpu %d is booting!\n", cpuid);
        for(int i = 0; i < 1000000; i++)
            __sync_fetch_and_add(&sum, 1);
            // sum++;
        printf("cpu %d report: sum = %d\n", cpuid, sum);
    }   
    while (1);
}  