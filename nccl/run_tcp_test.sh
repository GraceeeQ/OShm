#!/bin/bash

# 配置参数
MASTER_ADDR="localhost"  # 替换为主节点的实际IP
MASTER_PORT="29500"
WORLD_SIZE=2  # 总GPU数量
BACKEND="gloo"  # 使用gloo后端进行TCP/IP通信

# 启动测试
for ((i=0; i<WORLD_SIZE; i++)); do
    CUDA_VISIBLE_DEVICES=$i python tcp_gpu_comm_test.py \
        --rank $i \
        --world-size $WORLD_SIZE \
        --backend $BACKEND \
        --master-addr $MASTER_ADDR \
        --master-port $MASTER_PORT &
done

# 等待所有进程完成
wait
echo "TCP/IP通信测试完成！"