#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "custom_tcpdump.h"
#include <time.h>

// 全局变量，用于信号处理
static pcap_t *global_handle = NULL;

// 自定义数据包处理回调函数
void my_packet_handler(u_char *user, const struct pcap_pkthdr *header, const u_char *packet) {
    static int count = 1;
    
    // 打印包数量
    printf("捕获到的数据包 #%d:\n", count++);
    
    // 打印数据包时间戳
    printf("时间戳: %s", ctime((const time_t *)&header->ts.tv_sec));
    
    // 打印数据包大小
    printf("数据包长度: %d\n", header->len);
    
    // 简单地打印前10个字节
    printf("数据包内容 (前10个字节): ");
    for (int i = 0; i < 10 && i < header->caplen; i++) {
        printf("%02x ", packet[i]);
    }
    printf("\n\n");
}

// 信号处理函数，用于优雅地处理Ctrl+C
void signal_handler(int signum) {
    if (global_handle) {
        printf("\n捕获到终止信号，停止抓包...\n");
        pcap_breakloop(global_handle);
    }
}

int main(int argc, char *argv[]) {
    char *iface = NULL;
    char *filter = NULL;
    
    // 处理命令行参数
    if (argc < 2) {
        fprintf(stderr, "用法: %s <网络接口> [过滤规则]\n", argv[0]);
        fprintf(stderr, "例如: %s eth0 \"tcp port 80\"\n", argv[0]);
        return 1;
    }
    
    // 获取网络接口和可选的过滤规则
    iface = argv[1];
    if (argc >= 3) {
        filter = argv[2];
    }
    
    printf("开始在接口 %s 上抓包", iface);
    if (filter) {
        printf("，使用过滤规则: %s", filter);
    }
    printf("\n按Ctrl+C停止抓包\n\n");
    
    // 注册信号处理函数
    signal(SIGINT, signal_handler);
    
    // 使用自定义回调函数进行抓包，捕获数量为0表示无限制
    int result = custom_tcpdump_capture_with_handler(
        iface, filter, my_packet_handler, NULL, 0);
    
    if (result != 0) {
        fprintf(stderr, "抓包失败，错误码: %d\n", result);
        return 1;
    }
    
    printf("抓包完成\n");
    return 0;
}