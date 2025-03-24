#include <unistd.h>
#include <sys/syscall.h>

#define SYS_write_kv 450 
#define SYS_read_kv 451 

int _write_kv(int k, int v){
    printf("calling write_kv: k=%d, v=%d\n", k, v);
    int result = syscall(SYS_write_kv, k, v);
    printf("write_kv returned: %d\n", result);
    return result;
}
int _read_kv(int k){
    printf("calling read_kv: k=%d\n", k);
    int result = syscall(SYS_read_kv, k);
    printf("read_kv returned: %d\n", result);
    return result;
}