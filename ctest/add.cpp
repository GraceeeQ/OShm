#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#define SYS_addtotal 449 // 替换为你的系统调用号

int main() {
    int numdata = 10;
    long result = syscall(SYS_addtotal, numdata);
    printf("Result of sys_addtotal(%d): %ld\n", numdata, result);
    return 0;
}