#ifndef CUSTOM_TCPDUMP_H
#define CUSTOM_TCPDUMP_H

#include <pcap.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief 捕获包的回调函数类型定义
 * @param user 用户提供的数据
 * @param header 包头信息
 * @param packet 包数据
 */
typedef void (*packet_handler)(u_char *user, const struct pcap_pkthdr *header, const u_char *packet);

/**
 * @brief 使用自定义过滤规则对网络数据进行抓包
 * @param iface 需要进行抓包的网络接口名称
 * @param custom_filter 用户自定义的过滤表达式
 * @param buffer 存储抓取到的数据缓冲区
 * @param buffer_size 存储抓包数据的缓冲区大小
 * @return 成功时返回0，失败返回非0错误码
 */
int custom_tcpdump_capture(const char *iface, const char *custom_filter, void *buffer, size_t buffer_size);

/**
 * @brief 使用自定义过滤规则和回调函数对网络数据进行抓包
 * @param iface 需要进行抓包的网络接口名称
 * @param custom_filter 用户自定义的过滤表达式
 * @param handler 处理每个数据包的回调函数
 * @param user_data 传递给回调函数的用户数据
 * @param packet_count 抓取的数据包数量，0表示无限制
 * @return 成功时返回0，失败返回非0错误码
 */
int custom_tcpdump_capture_with_handler(const char *iface, const char *custom_filter, 
                                       packet_handler handler, u_char *user_data, int packet_count);

#endif /* CUSTOM_TCPDUMP_H */