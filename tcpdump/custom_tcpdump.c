#include "custom_tcpdump.h"
#include <errno.h>

// 用于存储抓取的数据包信息的结构体
typedef struct {
    void *buffer;
    size_t buffer_size;
    size_t bytes_written;
} capture_buffer_t;

// 默认的数据包处理回调函数，将数据包保存到提供的缓冲区中
static void default_packet_handler(u_char *user, const struct pcap_pkthdr *header, const u_char *packet) {
    capture_buffer_t *capture_info = (capture_buffer_t *)user;
    
    // 检查缓冲区是否有足够空间
    if (capture_info->bytes_written + header->caplen > capture_info->buffer_size) {
        // 缓冲区已满，无法继续写入
        return;
    }
    
    // 复制数据包到缓冲区
    memcpy((u_char *)capture_info->buffer + capture_info->bytes_written, packet, header->caplen);
    capture_info->bytes_written += header->caplen;
}

// 基本实现，使用提供的缓冲区存储抓取的数据包
int custom_tcpdump_capture(const char *iface, const char *custom_filter, void *buffer, size_t buffer_size) {
    if (!iface || !buffer || buffer_size == 0) {
        return -EINVAL; // 无效参数
    }
    
    // 创建capture_info结构体
    capture_buffer_t capture_info = {
        .buffer = buffer,
        .buffer_size = buffer_size,
        .bytes_written = 0
    };
    
    // 调用带有回调函数的版本
    return custom_tcpdump_capture_with_handler(iface, custom_filter, default_packet_handler, 
                                              (u_char *)&capture_info, 10); // 默认抓取10个包
}

// 高级版本，使用自定义回调函数处理抓取的数据包
int custom_tcpdump_capture_with_handler(const char *iface, const char *custom_filter, 
                                       packet_handler handler, u_char *user_data, int packet_count) {
    char errbuf[PCAP_ERRBUF_SIZE] = {0};
    pcap_t *handle = NULL;
    struct bpf_program fp;
    int ret = 0;
    
    // 参数检查
    if (!iface || !handler) {
        return -EINVAL;
    }
    
    // 打开网络接口进行抓包
    handle = pcap_open_live(iface, BUFSIZ, 1, 1000, errbuf);
    if (handle == NULL) {
        fprintf(stderr, "无法打开网络接口 %s: %s\n", iface, errbuf);
        return -ENODEV;
    }
    
    // 如果提供了过滤规则，则编译和应用它
    if (custom_filter && strlen(custom_filter) > 0) {
        if (pcap_compile(handle, &fp, custom_filter, 0, PCAP_NETMASK_UNKNOWN) == -1) {
            fprintf(stderr, "无法编译过滤表达式 '%s': %s\n", 
                   custom_filter, pcap_geterr(handle));
            pcap_close(handle);
            return -EINVAL;
        }
        
        if (pcap_setfilter(handle, &fp) == -1) {
            fprintf(stderr, "无法设置过滤器: %s\n", pcap_geterr(handle));
            pcap_freecode(&fp);
            pcap_close(handle);
            return -EINVAL;
        }
        
        pcap_freecode(&fp);
    }
    
    // 开始抓包
    ret = pcap_loop(handle, packet_count, handler, user_data);
    if (ret == -1) {
        fprintf(stderr, "抓包错误: %s\n", pcap_geterr(handle));
        pcap_close(handle);
        return -EIO;
    }
    
    // 关闭句柄
    pcap_close(handle);
    return 0;
}