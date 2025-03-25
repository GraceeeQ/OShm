#include "kv.h"
#include <map>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <cassert>

void test_basic_operations() {
    std::cout << "Testing basic operations..." << std::endl;

    // 测试写入和读取
    assert(write_kv(1, 100) == 0);
    assert(read_kv(1) == 100);

    // 测试覆盖写入
    assert(write_kv(1, 200) == 0);
    assert(read_kv(1) == 200);

    // 测试多个键值对
    assert(write_kv(2, 300) == 0);
    assert(write_kv(3, 400) == 0);
    assert(read_kv(2) == 300);
    assert(read_kv(3) == 400);

    // 测试不存在的键
    assert(read_kv(999) == -1); // 假设 -1 表示键不存在

    std::cout << "Basic operations passed!" << std::endl;
}

void test_edge_cases() {
    std::cout << "Testing edge cases..." << std::endl;

    // 测试负数键
    assert(write_kv(-1, 500) == 0);
    assert(read_kv(-1) == 500);

    // 测试大值
    assert(write_kv(1000, 1000000) == 0);
    assert(read_kv(1000) == 1000000);

    // 测试覆盖写入大值
    assert(write_kv(1000, 2000000) == 0);
    assert(read_kv(1000) == 2000000);

    std::cout << "Edge cases passed!" << std::endl;
}

void test_random_operations(int num_tests) {
    std::cout << "Testing random operations..." << std::endl;

    std::map<int, int> reference_map;
    srand(time(nullptr));

    for (int i = 0; i < num_tests; ++i) {
        int key = rand() % 1000; // 随机生成键
        int value = rand();      // 随机生成值

        // 写入键值对
        assert(write_kv(key, value) == 0);
        reference_map[key] = value;

        // 验证读取
        assert(read_kv(key) == value);
    }

    // 验证所有键值对
    for (const auto& [key, value] : reference_map) {
        assert(read_kv(key) == value);
    }

    std::cout << "Random operations passed!" << std::endl;
}

void test_large_scale() {
    std::cout << "Testing large-scale operations..." << std::endl;

    const int num_entries = 100000; // 测试 10 万个键值对
    for (int i = 0; i < num_entries; ++i) {
        assert(write_kv(i, i * 10) == 0);
    }

    for (int i = 0; i < num_entries; ++i) {
        assert(read_kv(i) == i * 10);
    }

    std::cout << "Large-scale operations passed!" << std::endl;
}

int main() {
    std::cout << "Running single thread tests..." << std::endl;
    test_basic_operations();
    test_edge_cases();
    test_random_operations(1000); // 测试 1000 次随机操作
    test_large_scale();

    std::cout << "All tests passed!" << std::endl;
    return 0;
}