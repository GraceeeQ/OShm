#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <cuda_runtime.h>
#include <nccl.h>

#define CUDA_CHECK(cmd) do {                         \
  cudaError_t err = cmd;                             \
  if (err != cudaSuccess) {                          \
    std::cerr << "CUDA error: " << cudaGetErrorString(err) \
              << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    exit(EXIT_FAILURE);                              \
  }                                                  \
} while(0)

#define NCCL_CHECK(cmd) do {                         \
  ncclResult_t res = cmd;                            \
  if (res != ncclSuccess) {                          \
    std::cerr << "NCCL error: " << ncclGetErrorString(res) \
              << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
    exit(EXIT_FAILURE);                              \
  }                                                  \
} while(0)

// 用于比较浮点数是否在容差范围内
bool is_close(float a, float b, float rtol = 1e-5, float atol = 1e-8) {
    return std::fabs(a - b) <= (atol + rtol * std::fabs(b));
}

void test_broadcast(int size) {
    int nDevices = 0;
    CUDA_CHECK(cudaGetDeviceCount(&nDevices));
    if (nDevices < 2) {
        std::cout << "需要至少2个GPU设备进行测试\n";
        return;
    }
    
    std::cout << "测试广播操作，数据大小: " << size << " 元素\n";
    
    // 为每个设备分配CUDA内存
    std::vector<float*> d_data(nDevices);
    std::vector<float*> h_results(nDevices);
    std::vector<cudaStream_t> streams(nDevices);
    std::vector<int> devs(nDevices);
    ncclComm_t* comms = new ncclComm_t[nDevices];
    
    // 初始化数据
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    
    std::vector<float> h_data(size);
    for (int i = 0; i < size; i++) {
        h_data[i] = dist(gen);
    }
    
    // 初始化设备
    for (int i = 0; i < nDevices; i++) {
        devs[i] = i;
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaMalloc(&d_data[i], size * sizeof(float)));
        CUDA_CHECK(cudaMemset(d_data[i], 0, size * sizeof(float)));
        h_results[i] = new float[size];
        CUDA_CHECK(cudaStreamCreate(&streams[i]));
    }
    
    // 初始化NCCL
    NCCL_CHECK(ncclCommInitAll(comms, nDevices, devs.data()));
    
    // 只在root设备上设置数据
    int root = 0;
    CUDA_CHECK(cudaSetDevice(root));
    CUDA_CHECK(cudaMemcpy(d_data[root], h_data.data(), size * sizeof(float), cudaMemcpyHostToDevice));
    
    // 执行广播
    NCCL_CHECK(ncclGroupStart());
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        NCCL_CHECK(ncclBroadcast(d_data[i], d_data[i], size, ncclFloat, root, comms[i], streams[i]));
    }
    NCCL_CHECK(ncclGroupEnd());
    
    // 同步并获取结果
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaStreamSynchronize(streams[i]));
        CUDA_CHECK(cudaMemcpy(h_results[i], d_data[i], size * sizeof(float), cudaMemcpyDeviceToHost));
    }
    
    // 验证结果
    bool all_correct = true;
    for (int i = 1; i < nDevices; i++) {
        bool device_correct = true;
        for (int j = 0; j < size; j++) {
            if (!is_close(h_results[0][j], h_results[i][j])) {
                std::cout << "GPU " << i << " 数据不一致: index " << j 
                          << ", 值 " << h_results[i][j] 
                          << ", 预期 " << h_results[0][j] << "\n";
                device_correct = false;
                all_correct = false;
                break;
            }
        }
        if (device_correct) {
            std::cout << "GPU " << i << " 广播数据验证通过\n";
        }
    }
    
    if (all_correct) {
        std::cout << "广播操作验证通过: 所有GPU数据一致\n";
    } else {
        std::cout << "广播操作验证失败: 数据不一致\n";
    }
    
    // 清理资源
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaFree(d_data[i]));
        delete[] h_results[i];
        CUDA_CHECK(cudaStreamDestroy(streams[i]));
        ncclCommDestroy(comms[i]);
    }
    delete[] comms;
}

void test_allreduce(int size) {
    int nDevices = 0;
    CUDA_CHECK(cudaGetDeviceCount(&nDevices));
    if (nDevices < 2) {
        std::cout << "需要至少2个GPU设备进行测试\n";
        return;
    }
    
    std::cout << "测试All-Reduce操作，数据大小: " << size << " 元素\n";
    
    // 为每个设备分配CUDA内存
    std::vector<float*> d_data(nDevices);
    std::vector<float*> h_input(nDevices);
    std::vector<float*> h_results(nDevices);
    std::vector<cudaStream_t> streams(nDevices);
    std::vector<int> devs(nDevices);
    ncclComm_t* comms = new ncclComm_t[nDevices];
    
    // 初始化数据
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    
    for (int i = 0; i < nDevices; i++) {
        h_input[i] = new float[size];
        for (int j = 0; j < size; j++) {
            h_input[i][j] = dist(gen);
        }
    }
    
    // 初始化设备
    for (int i = 0; i < nDevices; i++) {
        devs[i] = i;
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaMalloc(&d_data[i], size * sizeof(float)));
        CUDA_CHECK(cudaMemcpy(d_data[i], h_input[i], size * sizeof(float), cudaMemcpyHostToDevice));
        h_results[i] = new float[size];
        CUDA_CHECK(cudaStreamCreate(&streams[i]));
    }
    
    // 初始化NCCL
    NCCL_CHECK(ncclCommInitAll(comms, nDevices, devs.data()));
    
    // 执行All-Reduce (SUM)
    NCCL_CHECK(ncclGroupStart());
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        NCCL_CHECK(ncclAllReduce(d_data[i], d_data[i], size, ncclFloat, ncclSum, comms[i], streams[i]));
    }
    NCCL_CHECK(ncclGroupEnd());
    
    // 同步并获取结果
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaStreamSynchronize(streams[i]));
        CUDA_CHECK(cudaMemcpy(h_results[i], d_data[i], size * sizeof(float), cudaMemcpyDeviceToHost));
    }
    
    // 计算预期结果
    std::vector<float> expected(size, 0.0f);
    for (int i = 0; i < nDevices; i++) {
        for (int j = 0; j < size; j++) {
            expected[j] += h_input[i][j];
        }
    }
    
    // 验证结果
    bool all_correct = true;
    for (int i = 0; i < nDevices; i++) {
        bool device_correct = true;
        for (int j = 0; j < size; j++) {
            if (!is_close(expected[j], h_results[i][j])) {
                std::cout << "GPU " << i << " 数据不一致: index " << j 
                          << ", 值 " << h_results[i][j] 
                          << ", 预期 " << expected[j] 
                          << ", 差值 " << std::fabs(h_results[i][j] - expected[j]) << "\n";
                device_correct = false;
                all_correct = false;
                if (j > 10) break; // 只显示前几个错误
            }
        }
        if (device_correct) {
            std::cout << "GPU " << i << " All-Reduce数据验证通过\n";
        }
    }
    
    if (all_correct) {
        std::cout << "All-Reduce操作验证通过: 所有GPU数据一致\n";
    } else {
        std::cout << "All-Reduce操作验证失败: 数据不一致\n";
    }
    
    // 清理资源
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaFree(d_data[i]));
        delete[] h_input[i];
        delete[] h_results[i];
        CUDA_CHECK(cudaStreamDestroy(streams[i]));
        ncclCommDestroy(comms[i]);
    }
    delete[] comms;
}

int main() {
    // 测试不同数据大小
    std::vector<int> sizes = {1024, 1024 * 1024, 10 * 1024 * 1024};
    
    for (int size : sizes) {
        test_broadcast(size);
        std::cout << "-------------------\n";
        test_allreduce(size);
        std::cout << "===================\n";
    }
    
    return 0;
}