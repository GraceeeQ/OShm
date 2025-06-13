#include <iostream>
#include <vector>
#include <string>
#include <chrono>
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

// 测试一次通信操作的延迟
double measure_latency(ncclComm_t* comms, int nDevices, std::vector<float*>& d_data, 
                       std::vector<cudaStream_t>& streams, int size, ncclRedOp_t op, 
                       ncclDataType_t dataType, int iterations) {
    // 热身
    NCCL_CHECK(ncclGroupStart());
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        NCCL_CHECK(ncclAllReduce(d_data[i], d_data[i], size, dataType, op, comms[i], streams[i]));
    }
    NCCL_CHECK(ncclGroupEnd());
    
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaStreamSynchronize(streams[i]));
    }
    
    // 测量延迟
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; iter++) {
        NCCL_CHECK(ncclGroupStart());
        for (int i = 0; i < nDevices; i++) {
            CUDA_CHECK(cudaSetDevice(i));
            NCCL_CHECK(ncclAllReduce(d_data[i], d_data[i], size, dataType, op, comms[i], streams[i]));
        }
        NCCL_CHECK(ncclGroupEnd());
        
        for (int i = 0; i < nDevices; i++) {
            CUDA_CHECK(cudaSetDevice(i));
            CUDA_CHECK(cudaStreamSynchronize(streams[i]));
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::micro> elapsed = end - start;
    
    return elapsed.count() / iterations;  // 单次延迟（微秒）
}

// 测试带宽
double measure_bandwidth(ncclComm_t* comms, int nDevices, std::vector<float*>& d_data, 
                         std::vector<cudaStream_t>& streams, int size, ncclRedOp_t op, 
                         ncclDataType_t dataType, int iterations) {
    // 热身
    NCCL_CHECK(ncclGroupStart());
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        NCCL_CHECK(ncclAllReduce(d_data[i], d_data[i], size, dataType, op, comms[i], streams[i]));
    }
    NCCL_CHECK(ncclGroupEnd());
    
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaStreamSynchronize(streams[i]));
    }
    
    // 计算带宽
    auto start = std::chrono::high_resolution_clock::now();
    
    for (int iter = 0; iter < iterations; iter++) {
        NCCL_CHECK(ncclGroupStart());
        for (int i = 0; i < nDevices; i++) {
            CUDA_CHECK(cudaSetDevice(i));
            NCCL_CHECK(ncclAllReduce(d_data[i], d_data[i], size, dataType, op, comms[i], streams[i]));
        }
        NCCL_CHECK(ncclGroupEnd());
        
        for (int i = 0; i < nDevices; i++) {
            CUDA_CHECK(cudaSetDevice(i));
            CUDA_CHECK(cudaStreamSynchronize(streams[i]));
        }
    }
    
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = end - start;
    
    // 计算带宽: (数据大小 * 迭代次数 * 2(一次操作涉及的设备数)) / 时间
    // 对于AllReduce，每个GPU发送和接收 (n-1)*size/n 数据
    double bytes = size * sizeof(float) * iterations * 2 * (nDevices - 1) * nDevices / nDevices;
    return bytes / elapsed.count() / (1024 * 1024 * 1024);  // GB/s
}

void run_nccl_performance_tests() {
    int nDevices = 0;
    CUDA_CHECK(cudaGetDeviceCount(&nDevices));
    if (nDevices < 2) {
        std::cout << "需要至少2个GPU设备进行测试\n";
        return;
    }
    
    std::cout << "运行NCCL性能测试 (使用 " << nDevices << " 个GPU)\n";
    
    // 测试不同的数据大小
    std::vector<int> sizes = {
        8, 64, 256, 1024,                                // 小规模 (字节)
        4 * 1024, 16 * 1024, 64 * 1024,                  // 中规模
        256 * 1024, 1024 * 1024,                         // 大规模
        4 * 1024 * 1024, 16 * 1024 * 1024                // 超大规模
    };
    
    std::vector<float*> d_data(nDevices);
    std::vector<cudaStream_t> streams(nDevices);
    std::vector<int> devs(nDevices);
    ncclComm_t* comms = new ncclComm_t[nDevices];
    
    // 初始化设备
    for (int i = 0; i < nDevices; i++) {
        devs[i] = i;
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaStreamCreate(&streams[i]));
    }
    
    // 初始化NCCL
    NCCL_CHECK(ncclCommInitAll(comms, nDevices, devs.data()));
    
    // 为最大的数据大小分配内存
    int max_size = sizes.back();
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaMalloc(&d_data[i], max_size * sizeof(float)));
        CUDA_CHECK(cudaMemset(d_data[i], 0, max_size * sizeof(float)));
    }
    
    std::cout << "| 操作 | 大小(B) | 延迟(us) | 带宽(GB/s) | 吞吐量(GB/s) |\n";
    std::cout << "|------|---------|----------|------------|-------------|\n";
    
    // 测试不同数据大小的延迟和带宽
    for (int size_bytes : sizes) {
        int elements = size_bytes / sizeof(float);
        if (elements < 1) elements = 1;
        
        // 根据数据大小调整迭代次数
        int iterations = std::max(10, 10 * 1024 * 1024 / size_bytes);
        if (size_bytes > 1024 * 1024) iterations = std::max(10, iterations);
        
        // 测量AllReduce延迟
        double latency_us = measure_latency(comms, nDevices, d_data, streams, elements, ncclSum, ncclFloat, iterations);
        
        // 测量AllReduce带宽
        double bandwidth_gbs = measure_bandwidth(comms, nDevices, d_data, streams, elements, ncclSum, ncclFloat, iterations);
        
        // 计算吞吐量：数据大小/延迟
        double throughput_gbs = (size_bytes * 1e-9) / (latency_us * 1e-6);
        
        std::cout << "| AllReduce | " << size_bytes 
                  << " | " << latency_us 
                  << " | " << bandwidth_gbs 
                  << " | " << throughput_gbs << " |\n";
        
        // 测量Broadcast延迟
        latency_us = 0;
        bandwidth_gbs = 0;
        throughput_gbs = 0;
        
        // 热身
        NCCL_CHECK(ncclGroupStart());
        for (int i = 0; i < nDevices; i++) {
            CUDA_CHECK(cudaSetDevice(i));
            NCCL_CHECK(ncclBroadcast(d_data[i], d_data[i], elements, ncclFloat, 0, comms[i], streams[i]));
        }
        NCCL_CHECK(ncclGroupEnd());
        
        for (int i = 0; i < nDevices; i++) {
            CUDA_CHECK(cudaSetDevice(i));
            CUDA_CHECK(cudaStreamSynchronize(streams[i]));
        }
        
        // 测量延迟
        auto start = std::chrono::high_resolution_clock::now();
        
        for (int iter = 0; iter < iterations; iter++) {
            NCCL_CHECK(ncclGroupStart());
            for (int i = 0; i < nDevices; i++) {
                CUDA_CHECK(cudaSetDevice(i));
                NCCL_CHECK(ncclBroadcast(d_data[i], d_data[i], elements, ncclFloat, 0, comms[i], streams[i]));
            }
            NCCL_CHECK(ncclGroupEnd());
            
            for (int i = 0; i < nDevices; i++) {
                CUDA_CHECK(cudaSetDevice(i));
                CUDA_CHECK(cudaStreamSynchronize(streams[i]));
            }
        }
        
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::micro> elapsed = end - start;
        latency_us = elapsed.count() / iterations;
        
        // 计算带宽和吞吐量
        std::chrono::duration<double> elapsed_s = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed);
        double bytes = size_bytes * iterations * (nDevices - 1);  // root发送给其他n-1个设备
        bandwidth_gbs = bytes / elapsed_s.count() / (1024 * 1024 * 1024);
        throughput_gbs = (size_bytes * 1e-9) / (latency_us * 1e-6);
        
        std::cout << "| Broadcast | " << size_bytes 
                  << " | " << latency_us 
                  << " | " << bandwidth_gbs 
                  << " | " << throughput_gbs << " |\n";
    }
    
    // 清理资源
    for (int i = 0; i < nDevices; i++) {
        CUDA_CHECK(cudaSetDevice(i));
        CUDA_CHECK(cudaFree(d_data[i]));
        CUDA_CHECK(cudaStreamDestroy(streams[i]));
        ncclCommDestroy(comms[i]);
    }
    delete[] comms;
}

int main() {
    run_nccl_performance_tests();
    return 0;
}