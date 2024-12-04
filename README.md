# 实验3

## timer.c

### M-mode

**timer_init()-完成时钟中断的初始化设置任务**，并在start.c中被调用

- **MTIME**寄存器的值跟随物理时钟不断 +1
- 每个CPU都有自己的**mtimecmp**寄存器，这个寄存器每时每刻都会和 **MTIME** 寄存器做比较，一旦两者相同, 就会触发一次时钟中断, 程序流跳转到 **mtvec** 寄存器中存储的地址

1.初始化**mtimecmp**寄存器

`MTIMECMP = MTIME + INTERVAL`

2.在trap.S中的timer_vector()函数中：

```asm
# 暂存寄存器 a0 a1 a2 a3
# 将MSR寄存器 mscratch 放入 a0
csrrw a0, mscratch, a0
sd a1, 0(a0)      # mscratch[0] = a1
sd a2, 8(a0)      # mscratch[1] = a2
sd a3, 16(a0)     # mscratch[2] = a3

# CLINT_MTIMECMP(hartid) = CLINT_MTIMECMP(hartid) + INTERVAL
# 以便响应下一次时钟中断
ld a1, 24(a0)     # a1 = mscratch[3] 里面放了 CLINT_MTIMECMP(hartid)
ld a2, 32(a0)     # a2 = mscratch[4] 里面放了 INTERVAL 
```

所以需要在timer_init()函数中，

`mscratch[3] = CLINT_MTIMECMP(hartid)`

`mscratch[4] = INTERVAL` 

3.将设置好的mscratch写入MSR寄存器 mscratch

4.将时钟中断处理程序的入口地址timer_vector写入 mtvec 寄存器 (机器模式)

5.启用机器模式下的中断

6.启用机器模式下的时钟中断

### S-mode

**维护系统时钟**

- timer_create()-时钟创建(初始化系统时钟)
- timer_update()-时钟更新(ticks++ with lock)
- timer_get_ticks()-返回系统时钟ticks

## trap_ternel.c

外设中断处理 (基于PLIC)

```c
// 获取中断号
int irq = plic_claim();
if(irq == UART_IRQ) uart_intr();
else if(irq) printf("unexpected interrupt irq=%d\n", irq);
// 中断已完成
if(irq) plic_complete(irq);
```

时钟中断处理 (基于CLINT)

```c
if(mycpuid() == 0){
    // 系统时钟的更新只需要单个CPU去做即可
	timer_update();
	// printf("cpu %d: di da\n",r_tp());
	// printf("ticks = %d\n",timer_get_ticks());
}
// 清除 sip 寄存器中的SSIP位（第1位）来确认软件中断
w_sip(r_sip() & ~2);
```

内核态trap处理的核心逻辑

```c
uint64 sepc = r_sepc();          // 记录了发生异常时的pc值
uint64 sstatus = r_sstatus();    // 与特权模式和中断相关的状态信息
uint64 scause = r_scause();      // 引发trap的原因
uint64 stval = r_stval();        // 发生trap时保存的附加信息(不同trap不一样)

// 确认trap来自S-mode且此时trap处于关闭状态
assert(sstatus & SSTATUS_SPP, "trap_kernel_handler: not from s-mode");
assert(intr_get() == 0, "trap_kernel_handler: interreput enabled");

int trap_id = scause & 0xf; 

// 中断异常处理核心逻辑
if(scause & 0x8000000000000000L){
	switch (trap_id)
	{
    case 1: // 时钟中断
        timer_interrupt_handler();
        break;
    case 9: // 外部中断
        external_interrupt_handler();
        break;
    default: // 其他中断
        printf("%s\n",interrupt_info[trap_id]);
        break;
    }
}else{
    // 异常
    printf("%s\n",exception_info[trap_id]);
    printf("scause %p\n", scause);
    printf("stval %p\n", stval);
    printf("sepc=%p stval=%p\n", r_sepc(), r_stval());
    panic("kerneltrap");
}

// 后面会用到
w_sepc(sepc);
w_sstatus(sstatus);
```