import pandas as pd
import matplotlib.pyplot as plt
import numpy as np
import sys
import re

def parse_table(file_path):
    """从文件中解析表格数据"""
    with open(file_path, 'r') as f:
        content = f.read()
    
    # 使用正则表达式提取表格行
    lines = re.findall(r'\|\s*(\w+)\s*\|\s*(\d+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|\s*([\d\.]+)\s*\|', content)
    
    data = []
    for op, size, latency, bandwidth, throughput in lines:
        data.append({
            'operation': op,
            'size_bytes': int(size),
            'latency_us': float(latency),
            'bandwidth_gbs': float(bandwidth),
            'throughput_gbs': float(throughput)
        })
    
    return pd.DataFrame(data)

def plot_comparison(nccl_df, tcp_df):
    """绘制NCCL和TCP/IP性能比较图表"""
    operations = ['AllReduce', 'Broadcast']
    
    for op in operations:
        # 过滤操作类型
        nccl_op_df = nccl_df[nccl_df['operation'].str.lower() == op.lower()]
        tcp_op_df = tcp_df[tcp_df['operation'].str.lower() == op.lower()]
        
        # 创建图表
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
        
        # 设置x轴数据
        sizes_mb = [size/1024/1024 for size in nccl_op_df['size_bytes']]
        x_pos = np.arange(len(sizes_mb))
        width = 0.35
        
        # 带宽对比
        ax1.bar(x_pos - width/2, nccl_op_df['bandwidth_gbs'], width, label='NCCL')
        ax1.bar(x_pos + width/2, tcp_op_df['bandwidth_gbs'], width, label='TCP/IP')
        ax1.set_ylabel('Bandwidth (GB/s)')
        ax1.set_title(f'{op} Bandwidth Comparison')
        ax1.set_xticks(x_pos)
        ax1.set_xticklabels([f'{s:.2f}' for s in sizes_mb])
        ax1.set_xlabel('Data Size (MB)')
        ax1.legend()
        
        # 延迟对比 (对数尺度)
        ax2.set_yscale('log')
        ax2.bar(x_pos - width/2, nccl_op_df['latency_us'], width, label='NCCL')
        ax2.bar(x_pos + width/2, tcp_op_df['latency_us'], width, label='TCP/IP')
        ax2.set_ylabel('Latency (us)')
        ax2.set_title(f'{op} Latency Comparison')
        ax2.set_xticks(x_pos)
        ax2.set_xticklabels([f'{s:.2f}' for s in sizes_mb])
        ax2.set_xlabel('Data Size (MB)')
        ax2.legend()
        
        plt.tight_layout()
        plt.savefig(f'{op.lower()}_comparison.png', dpi=300)
        
        print(f"Saved {op} comparison chart to {op.lower()}_comparison.png")
    
    # 创建加速比图表
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(16, 6))
    
    for op in operations:
        nccl_op_df = nccl_df[nccl_df['operation'].str.lower() == op.lower()]
        tcp_op_df = tcp_df[tcp_df['operation'].str.lower() == op.lower()]
        
        sizes_mb = [size/1024/1024 for size in nccl_op_df['size_bytes']]
        
        # 带宽加速比
        bandwidth_speedup = nccl_op_df['bandwidth_gbs'].values / tcp_op_df['bandwidth_gbs'].values
        ax1.plot(sizes_mb, bandwidth_speedup, marker='o', label=op)
        
        # 延迟加速比 (TCP延迟/NCCL延迟，值越大表示NCCL越快)
        latency_speedup = tcp_op_df['latency_us'].values / nccl_op_df['latency_us'].values
        ax2.plot(sizes_mb, latency_speedup, marker='x', label=op)
    
    ax1.set_xlabel('Data Size (MB)')
    ax1.set_ylabel('Bandwidth Speedup (NCCL/TCP)')
    ax1.set_title('NCCL vs TCP/IP Bandwidth Speedup')
    ax1.set_xscale('log')
    ax1.grid(True)
    ax1.legend()
    
    ax2.set_xlabel('Data Size (MB)')
    ax2.set_ylabel('Latency Speedup (TCP/NCCL)')
    ax2.set_title('NCCL vs TCP/IP Latency Speedup')
    ax2.set_xscale('log')
    ax2.grid(True)
    ax2.legend()
    
    plt.tight_layout()
    plt.savefig('nccl_tcp_speedup.png', dpi=300)
    print("Saved speedup chart to nccl_tcp_speedup.png")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python compare_results.py <NCCL_result_file> <TCP_result_file>")
        sys.exit(1)
    
    nccl_df = parse_table(sys.argv[1])
    tcp_df = parse_table(sys.argv[2])
    
    plot_comparison(nccl_df, tcp_df)