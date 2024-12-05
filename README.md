# 实验4

trapframe保存的是：

- 内核页表的物理地址
- 进程内核栈的顶部地址（内核态的存储器信息保存在栈中）
- 导致trap的函数地址
- 用户程序计数器（Program Counter）的值，即发生trap时用户代码的指令地址
- 内核硬件线程ID（HART ID）
- 从 `ra` 到 `t6` 的寄存器保存了**用户态**下的通用寄存器值

sd 保存过程

ld 恢复过程

## 用户态发生trap

用户中断产生->user_vector(内核页表写入satp寄存器，处理器将使用新的页表-内核页表，从而能够访问内核空间的内存地址 用户态转内核态)->trap_user_handler()->trap_user_return()->用户态陷阱处理完成,user_return(内核态转用户态)

### 用户态->内核态

- 根据trapframe的内核部分信息，恢复上一次内核的情况
- 使用trapframe保存此次用户态的寄存器信息：a[0] = sscratch寄存器(p->trapframe)->把需要保存的寄存器的值放到a[0]中->再把a[0]写回到sscratch寄存器寄存器

### 内核态->用户态

- 根据trapframe中的栈顶指针，恢复上一次用户态存储器的情况
- 使用trapframe保存此次内核部分信息

```c
	//设置 trapframe 中的字段，以便在下一次进程进入内核时能够恢复正确的内核态上下文。
    p->tf->kernel_satp = r_satp();         // 内核页表
    p->tf->kernel_sp = p->kstack + PGSIZE; // 进程的内核栈
    p->tf->kernel_trap = (uint64)trap_user_handler; // trap处理程序
    p->tf->kernel_hartid = r_tp();         // cpu id
    // 修改 sstatus 寄存器
    unsigned long x = r_sstatus();
    x &= ~SSTATUS_SPP; // SPP 位清零以设置为用户模式
    x |= SSTATUS_SPIE; // 启用用户模式下的中断 = intr_on()
    w_sstatus(x);
    // 将 sepc 寄存器设置为用户态程序计数器，指向用户程序被中断时的下一条指令。
    w_sepc(p->tf->epc);
    // 设置用户页表
    uint64 satp = MAKE_SATP(p->pgtbl);
```

