#include "riscv.h"
#include "lib/print.h"
#include "lib/str.h"
#include "dev/vio.h"
#include "mem/pmem.h"
#include "mem/vmem.h"
#include "mem/mmap.h"
#include "proc/proc.h"
#include "trap/trap.h"

volatile static int started = 0;

int main()
{
    int cpuid = r_tp();

    if(cpuid == 0) {
        
        print_init();
        printf("cpu %d is booting!\n", cpuid);
        pmem_init();
        kvm_init();
        kvm_inithart();
        trap_kernel_init();
        trap_kernel_inithart();        
        mmap_init();
        virtio_disk_init();
        proc_init();
        proc_make_first();

        __sync_synchronize();
        // started = 1;
    } else {

        while(started == 0);
        __sync_synchronize();
        
        printf("cpu %d is booting!\n", cpuid);
        kvm_inithart();
        trap_kernel_inithart();
    }
    proc_scheduler();
    while (1);
}