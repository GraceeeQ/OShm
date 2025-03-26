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
std::map<int, int> reference_map;
std::set<int> keys;
std::mutex keys_mutex;
std::mutex ref_map_mutex;

// 定义线程数量
const int NUM_THREADS = 10;
const int NUM_OPERATIONS = 100; // 每个线程执行的操作次数
// 写线程函数
void *write_thread(void *arg)
{
    int thread_id = *(int *)arg;
    std::cout << "write_thread: " << thread_id << std::endl;
    srand(time(nullptr) + thread_id); // 为每个线程设置不同的随机种子

    for (int i = 0; i < NUM_OPERATIONS; ++i)
    {
        int key = rand() % 4000; // 随机生成键
        int value = rand();      // 随机生成值

        // 写入到 kv_store
        int result = write_kv(key, value);
        assert(result == 0);
        std::lock_guard<std::mutex> lock(ref_map_mutex);
        reference_map[key] = value;
    }

    return nullptr;
}

// 读线程函数
void *read_thread(void *arg)
{
    int thread_id = *(int *)arg;
    std::cout << "read_thread: " << thread_id << std::endl;
    srand(time(nullptr) + thread_id); // 为每个线程设置不同的随机种子

    for (int i = 0; i < NUM_OPERATIONS; ++i)
    {
        int key = rand() % 4000; // 随机生成键
        // 从 kv_store 读取
        int value = read_kv(key);
        // 验证读取结果
        {
            std::lock_guard<std::mutex> lock(ref_map_mutex);
            auto it = reference_map.find(key);
            if (it != reference_map.end())
            {
                // 键存在，验证值是否正确
                if (value != it->second)
                {
                    std::cout << "key: " << key << ", value: " << value << ", expected: " << it->second << std::endl;
                    assert(false);
                }
            }
            else
            {
                // 键不存在，验证返回值是否为 -1
                // std::cout << "\nin c read_kv: k=" << key << ", v=" << value << std::endl;
                assert(value == -2 || value == -3 || value == -1);
            }
        }
        // std::cout << "read_kv: k=" << key << ", v=" << value << std::endl;
    }

    return nullptr;
}

int main()
{
    pthread_t threads[NUM_THREADS];
    int thread_ids[NUM_THREADS];

    // 创建写线程
    for (int i = 0; i < NUM_THREADS / 2; ++i)
    {
        thread_ids[i] = i;
        pthread_create(&threads[i], nullptr, write_thread, &thread_ids[i]);
    }

    for (int i = 0; i < NUM_THREADS / 2; ++i)
    {
        pthread_join(threads[i], nullptr);
    }
    // 创建读线程
    for (int i = NUM_THREADS / 2; i < NUM_THREADS; ++i)
    {
        thread_ids[i] = i;
        pthread_create(&threads[i], nullptr, read_thread, &thread_ids[i]);
    }

    // 等待所有线程完成
    for (int i = NUM_THREADS / 2; i < NUM_THREADS; ++i)
    {
        pthread_join(threads[i], nullptr);
    }

    std::cout << "All threads completed. All tests passed!" << std::endl;

    return 0;
}