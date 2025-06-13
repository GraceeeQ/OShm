import os
import time
import argparse
import torch
import torch.distributed as dist
import numpy as np
import datetime

def parse_args():
    parser = argparse.ArgumentParser(description='TCP/IP GPU通信测试 (对比NCCL)')
    parser.add_argument('--backend', default='gloo', type=str, help='分布式后端 (gloo)')
    parser.add_argument('--master-addr', default='localhost', type=str, help='主节点地址')
    parser.add_argument('--master-port', default='29500', type=str, help='主节点端口')
    parser.add_argument('--world-size', default=2, type=int, help='进程总数(GPU总数)')
    parser.add_argument('--rank', default=0, type=int, help='当前进程的rank')
    return parser.parse_args()

def init_process(args):
    # 初始化分布式环境
    os.environ['MASTER_ADDR'] = args.master_addr
    os.environ['MASTER_PORT'] = args.master_port
    
    # 初始化进程组
    dist.init_process_group(
        backend=args.backend,
        init_method=f'tcp://{args.master_addr}:{args.master_port}',
        world_size=args.world_size,
        rank=args.rank
    )
    
    print(f"初始化进程: rank {args.rank} / {args.world_size}, 后端: {args.backend}")
    if torch.cuda.is_available():
        device_id = args.rank % torch.cuda.device_count()
        torch.cuda.set_device(device_id)
        print(f"使用GPU: {torch.cuda.get_device_name(device_id)}")
    else:
        print("警告: 未检测到GPU，使用CPU")

def measure_latency(tensor, op_name, iterations):
    """测量延迟"""
    # 预热
    if op_name == "broadcast":
        dist.broadcast(tensor, src=0)
    elif op_name == "allreduce":
        dist.all_reduce(tensor, op=dist.ReduceOp.SUM)
    
    torch.cuda.synchronize()
    dist.barrier()
    
    # 测量延迟
    start = time.time()
    for _ in range(iterations):
        if op_name == "broadcast":
            dist.broadcast(tensor, src=0)
        elif op_name == "allreduce":
            dist.all_reduce(tensor, op=dist.ReduceOp.SUM)
        torch.cuda.synchronize()
    
    dist.barrier()
    end = time.time()
    
    latency_us = (end - start) * 1000000 / iterations
    return latency_us

def measure_bandwidth(tensor, op_name, iterations, world_size):
    """测量带宽"""
    # 元素总数和字节数
    size_bytes = tensor.numel() * tensor.element_size()
    
    # 预热
    if op_name == "broadcast":
        dist.broadcast(tensor, src=0)
    elif op_name == "allreduce":
        dist.all_reduce(tensor, op=dist.ReduceOp.SUM)
    
    torch.cuda.synchronize()
    dist.barrier()
    
    # 测量带宽
    start = time.time()
    for _ in range(iterations):
        if op_name == "broadcast":
            dist.broadcast(tensor, src=0)
        elif op_name == "allreduce":
            dist.all_reduce(tensor, op=dist.ReduceOp.SUM)
        torch.cuda.synchronize()
    
    dist.barrier()
    end = time.time()
    
    elapsed_s = end - start
    
    # 根据NCCL测试中的带宽计算方式
    if op_name == "broadcast":
        bytes_transferred = size_bytes * iterations * (world_size - 1)  # root发送给其他设备
    elif op_name == "allreduce":
        # AllReduce: 每个GPU发送和接收(n-1)*size/n数据
        bytes_transferred = size_bytes * iterations * 2 * (world_size - 1) * world_size / world_size
    
    bandwidth_gbs = bytes_transferred / elapsed_s / (1024 * 1024 * 1024)
    return bandwidth_gbs

def run_tcp_performance_tests(args):
    """运行性能测试"""
    if torch.cuda.is_available():
        device = torch.device(f"cuda:{args.rank % torch.cuda.device_count()}")
    else:
        device = torch.device("cpu")
    
    # 使用与NCCL测试相同的数据大小
    sizes_bytes = [
        8, 64, 256, 1024,                   # 小规模 (字节)
        4 * 1024, 16 * 1024, 64 * 1024,     # 中规模
        256 * 1024, 1024 * 1024,            # 大规模
        4 * 1024 * 1024, 16 * 1024 * 1024   # 超大规模
    ]
    
    operations = ["allreduce", "broadcast"]
    
    # 仅在rank 0上打印表头
    if args.rank == 0:
        print("| 操作 | 大小(B) | 延迟(us) | 带宽(GB/s) | 吞吐量(GB/s) |")
        print("|------|---------|----------|------------|-------------|")
    
    for size_bytes in sizes_bytes:
        # 计算元素数量 (假设为float32)
        elements = max(1, size_bytes // 4)
        
        # 根据数据大小调整迭代次数
        iterations = max(10, 10 * 1024 * 1024 // size_bytes)
        if size_bytes > 1024 * 1024:
            iterations = max(10, iterations)
        
        # 创建测试用张量
        tensor = torch.randn(elements, dtype=torch.float32, device=device)
        
        for op_name in operations:
            # 测量延迟
            latency_us = measure_latency(tensor, op_name, iterations)
            
            # 测量带宽
            bandwidth_gbs = measure_bandwidth(tensor, op_name, iterations, args.world_size)
            
            # 计算吞吐量
            throughput_gbs = (size_bytes * 1e-9) / (latency_us * 1e-6)
            
            # 仅在rank 0上打印结果
            if args.rank == 0:
                print(f"| {op_name} | {size_bytes} | {latency_us:.2f} | {bandwidth_gbs:.4f} | {throughput_gbs:.4f} |")

if __name__ == "__main__":
    args = parse_args()
    init_process(args)
    run_tcp_performance_tests(args)
    dist.destroy_process_group()