#include "kv.h"
#include <map>
#include <set>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <pthread.h>
#include <cassert>
#include <mutex>

// 全局参考数据结构和锁
// std::map<int, int> reference_map;
// std::set<int> keys;
// std::mutex keys_mutex;
// std::mutex ref_map_mutex;
// 定义屏障
pthread_barrier_t barrier;
// 定义线程数量
const int NUM_THREADS = 2;
const int NUM_OPERATIONS = 100; // 每个线程执行的操作次数

// 写线程函数
void *write_thread(void *arg)
{
    std::cout << "write_thread" << std::endl;
    pthread_barrier_wait(&barrier);
    for (int i = 0; i < NUM_OPERATIONS; ++i)
    {
        int key = i;
        int value = i * 10; // 随机生成值
        int result = write_kv(key, value);
        assert(result == 0); // 验证写入成功
        // int read_value = read_kv(key);
        // assert(read_value == value); // 验证写入正确
    }
    
    return nullptr;
}

// 读线程函数
void *read_thread(void *arg)
{
    std::cout << "read_thread" << std::endl;
    pthread_barrier_wait(&barrier);
    for (int i = 0; i < NUM_OPERATIONS; ++i)
    {
        int key = i;

        // 从 kv_store 读取
        int value = read_kv(key);

        // assert(value == key * 10);
    }

    return nullptr;
}

pthread_t threads[NUM_THREADS];
int thread_ids[NUM_THREADS];
int main()
{
    pthread_barrier_init(&barrier, nullptr, NUM_THREADS);
    pthread_create(&threads[0], nullptr, write_thread, nullptr);
    pthread_create(&threads[1], nullptr, read_thread, nullptr);
    pthread_join(threads[0], nullptr);
    pthread_join(threads[1], nullptr);
    pthread_barrier_destroy(&barrier);
    std::cout << "All threads completed. All tests passed!" << std::endl;

    return 0;
}