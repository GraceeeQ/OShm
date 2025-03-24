#include <stdio.h>
#include "kv.h"

int main() {
    int k = 1, v = 2;
    _write_kv(k, v);
    int result = _read_kv(k);
    printf("Result of write_kv(%d, %d) and read_kv(%d): %d\n", k, v, k, result);
    return 0;
}