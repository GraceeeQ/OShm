nvcc -o correctness_test correctness_test.cu -lnccl

# 编译NCCL测试
nvcc -o nccl_perf_test nccl_perf_test.cu -lnccl -lcudart

# 运行NCCL测试并保存结果
./nccl_perf_test > nccl_results.txt

# 确保PyTorch已安装
# 设置运行权限
chmod +x run_tcp_test.sh

# 运行TCP/IP测试
./run_tcp_test.sh > tcp_results.txt

python compare_results.py nccl_results.txt tcp_results.txt

