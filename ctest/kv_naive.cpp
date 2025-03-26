#include "kv.h"
#include <iostream>
#include <cassert>

void test_write_kv(int key, int value) {
    int result = write_kv(key, value);
    if (result == 0) {
    //     std::cout << "write_kv(" << key << ", " << value << ") succeeded." << std::endl;
    } else {
        std::cout<<"write errno"<<errno<<std::endl;
    //     std::cerr << "write_kv(" << key << ", " << value << ") failed with error: " << result << std::endl;
    }
}

void test_read_kv(int key, int expected_value) {
    int result = read_kv(key);
    if (result == expected_value) {
    //     std::cout << "read_kv(" << key << ") returned correct value: " << result << std::endl;
    // } else if (result == -1) {
    //     std::cerr << "read_kv(" << key << ") failed: key not found." << std::endl;
    } else {
        std::cout<<"read errno"<<errno<<std::endl;
    //     std::cerr << "read_kv(" << key << ") returned incorrect value: " << result
    //               << " (expected: " << expected_value << ")" << std::endl;
    }
}

int main() {
    // 测试写入和读取
    test_write_kv(1, 100);
    test_read_kv(1, 100);

    // // 测试覆盖写入
    // test_write_kv(1, 200);
    // test_read_kv(1, 200);

    // // 测试多个键值对
    // test_write_kv(2, 300);
    // test_write_kv(3, 400);
    // test_read_kv(2, 300);
    // test_read_kv(3, 400);

    // // 测试不存在的键
    // test_read_kv(999, -1); // 假设 -1 表示键不存在

    // // 测试负数键
    // test_write_kv(-1, 500);
    // test_read_kv(-1, 500);

    // // 测试大值
    // test_write_kv(1000, 1000000);
    // test_read_kv(1000, 1000000);

    // std::cout << "All tests completed." << std::endl;
    return 0;
}