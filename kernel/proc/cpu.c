#include "proc/cpu.h"
#include "riscv.h"
#include "lib/lock.h"

static cpu_t cpus[NCPU];

cpu_t* mycpu(void)
{
    int id = mycpuid();
    struct cpu *c = &cpus[id];
    return c;
}

int mycpuid(void) 
{
    int id = r_tp();
    return id;
}

// 返回当前cpu上运行的进程
proc_t* myproc(void)
{
    push_off();
    struct cpu *c = mycpu();
    proc_t *proc = c->proc;
    pop_off();
    return proc;
}