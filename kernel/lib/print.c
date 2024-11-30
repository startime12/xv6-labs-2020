// 标准输出和报错机制
#include <stdarg.h>
#include "lib/print.h"
#include "lib/lock.h"
#include "dev/uart.h"
#define BACKSPACE 0x100

volatile int panicked = 0;

static spinlock_t print_lk;

static char digits[] = "0123456789abcdef";

void print_init(void)
{
    uart_init();
    spinlock_init(&print_lk, "pr");
}

void consputc(int c){
    // 如果是退格键，用空格覆盖 '1|' -> '|1' -> ' |' -> '| '  
    if(c == BACKSPACE){
        uart_putc_sync('\b');
        uart_putc_sync(' ');
        uart_putc_sync('\b');
    }else{
        uart_putc_sync(c);
    }
}

// 输出一个int类型数字
void printint(int xx, int base, int sign){
    uint32 x;

    // sign 意为是否需要考虑符号
    // 注意这里是sign = xx < 0，而不是(sign = xx) < 0
    if(sign && (sign = xx < 0)){
        x = -xx;
    }else{
        x = xx;
    }

    int i = 0;
    char buf[16]; // 从后往前记录
    do{
        buf[i++] = digits[x % base];
    }while ((x /= base) != 0);
    
    if(sign){
        buf[i++] = '-';
    }

    while ( --i >= 0)
    {
        consputc(buf[i]);
    }
    
}

void printptr(uint64 x){
    consputc('0');
    consputc('x');

    //从高位到低位转化成16进制表示
    for(int i=0;i<(sizeof(uint64)*2);i++,x<<=4)
    consputc(digits[x >> (sizeof(uint64) * 8 - 4)]);
}

// Print to the console. only understands %d, %x, %p, %s.
void printf(const char *fmt, ...)
{
    va_list ap;
    int c;
    char *s;

    spinlock_acquire(&print_lk);

    // printf的字符串为空
    if(fmt == 0){
        panic("null fmt");
    }

    // va_start使ap指向可变参数列表的第一个参数
    va_start(ap,fmt);
    // & 0xff 将扩展ASCII字符转换为标准ASCII范围：保留字符的低 8 位，而忽略高 24 位
    for(int i = 0; (c = fmt[i] & 0xff) != 0;i++){
        if(c != '%'){
            consputc(c);
            continue;
        }
        // 取下一个字符
        c = fmt[++i] & 0xff;
        if(c == 0)break;
        switch (c)
        {
        case 'd':
            printint(va_arg(ap, int), 10 ,1);
            break;
        
        case 'x':
            printint(va_arg(ap, int), 16 ,1);
            break;
            
        case 'p':
            printptr(va_arg(ap, uint64));
            break;
            
        case 's':
            if( (s = va_arg(ap, char*)) == 0)
                s = "(null)";
            for( ; *s; s++)
                consputc(*s);
            break;
            
        case '%':
            consputc('%');
            break;

        default:
            consputc('%');
            consputc(c);
            break;
        }
    }
    
    spinlock_release(&print_lk);
}

void panic(const char *s)
{
    printf("panic: ");
    printf(s);
    printf("\n");
    panicked = 1;// 避免有其他输出
}

void assert(bool condition, const char* warning)
{
    if(!condition)
    {
        printf("Assertion failed: %s\n",warning);
    }
}
