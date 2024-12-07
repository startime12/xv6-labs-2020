// 这个头文件通常认为其他.h文件都应该include
#ifndef __COMMON_H__
#define __COMMON_H__

// 类型定义

typedef char                   int8;
typedef short                  int16;
typedef int                    int32;
typedef long long              int64;
typedef unsigned char          uint8; 
typedef unsigned short         uint16;
typedef unsigned int           uint32;
typedef unsigned long long     uint64;

typedef unsigned long long         reg; 
typedef enum {false = 0, true = 1} bool;

#ifndef NULL
#define NULL ((void*)0)
#endif

// OS 全局变量

#define NCPU 2         // 最大CPU数量    
#define NPROC 64       // 最大进程数量
#define PGSIZE 4096    // 物理页大小


// 帮助

#define MAX(a,b) ((a) > (b) ? (a) : (b)) // 取大
#define MIN(a,b) ((a) < (b) ? (a) : (b)) // 取小

#define ALIGN_UP(addr,refer) (((addr) + (refer) - 1) & ~((refer) - 1)) // 向上对齐
#define ALIGN_DOWN(addr,refer) ((addr) & ~((refer) - 1)) // 向下对齐

#endif