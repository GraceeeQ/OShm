# 自定义 tcpdump 抓包工具使用说明

## 项目概述

这个项目实现了一个基于 libpcap 的定制化网络数据包捕获工具，扩展了标准 tcpdump 的功能，提供了更灵活的编程接口，允许用户自定义过滤规则和数据包处理逻辑。

## 支持的过滤规则格式

本工具支持标准的 Berkeley Packet Filter (BPF) 过滤表达式语法，与原生 tcpdump 使用的过滤语法完全兼容。以下是一些常用的过滤规则格式和示例：

### 基本语法结构

过滤表达式通常遵循以下格式：
```
[协议] [方向] [类型] [值]
```

### 按协议过滤

```
tcp               # 只捕获TCP协议数据包
udp               # 只捕获UDP协议数据包
icmp              # 只捕获ICMP协议数据包
ip                # 只捕获IPv4数据包
ip6               # 只捕获IPv6数据包
arp               # 只捕获ARP数据包
```

### 按端口过滤

```
port 80           # 捕获所有端口80的流量(TCP或UDP)
tcp port 80       # 只捕获TCP端口80的流量(HTTP)
udp port 53       # 只捕获UDP端口53的流量(DNS)
port 80 or port 443    # 捕获HTTP或HTTPS流量
```

### 按主机/IP地址过滤

```
host 192.168.1.1       # 捕获与特定IP相关的所有流量
src host 192.168.1.1   # 只捕获源IP为指定地址的流量
dst host 192.168.1.1   # 只捕获目标IP为指定地址的流量
```

### 按网络范围过滤

```
net 192.168.0.0/24     # 捕获特定子网内的所有流量
src net 10.0.0.0/8     # 捕获来自特定网络的流量
```

### 复合条件过滤

```
tcp port 80 and host 192.168.1.1    # 同时满足协议、端口和主机条件
tcp port 80 or tcp port 443         # 满足任一端口条件
tcp and not port 22                 # 排除特定端口
```

### 按数据包大小过滤

```
greater 1024           # 捕获大于1024字节的数据包
less 64                # 捕获小于64字节的数据包
```

### 按以太网MAC地址过滤

```
ether host 00:11:22:33:44:55        # 捕获特定MAC地址相关的流量
ether src 00:11:22:33:44:55         # 只捕获源MAC为指定地址的流量
```

### 按特殊类型过滤

```
broadcast              # 只捕获广播数据包
multicast              # 只捕获多播数据包
```

### 按TCP标志过滤

```
tcp[tcpflags] & tcp-syn != 0        # 捕获SYN包(连接建立)
tcp[tcpflags] & tcp-fin != 0        # 捕获FIN包(连接终止)
tcp[tcpflags] & (tcp-syn|tcp-ack) == (tcp-syn|tcp-ack)  # 捕获SYN+ACK包
```

## 使用示例

使用我们提供的命令行工具：

```bash
# 捕获所有HTTP流量
./custom_tcpdump eth0 "tcp port 80"

# 捕获特定主机的所有流量
./custom_tcpdump wlan0 "host 192.168.1.100"

# 捕获DNS查询
./custom_tcpdump any "udp port 53"

# 捕获HTTP和HTTPS流量但排除特定主机
./custom_tcpdump eth0 "(tcp port 80 or tcp port 443) and not host 192.168.1.5"
```

## 注意事项

1. 过滤规则必须使用引号括起来，以避免shell对特殊字符的解释
2. 某些过滤规则可能需要管理员权限才能正常工作
3. 复杂的过滤规则应当谨慎测试，确保符合预期效果
4. 一些高级过滤语法可能依赖于特定版本的libpcap库支持

## 编程接口

该工具提供了两个主要的函数接口供开发者使用：

```c
// 基本接口：将捕获的数据包存储到提供的缓冲区
int custom_tcpdump_capture(const char *iface, const char *custom_filter, 
                          void *buffer, size_t buffer_size);

// 高级接口：使用自定义回调函数处理捕获的数据包
int custom_tcpdump_capture_with_handler(const char *iface, const char *custom_filter, 
                                       packet_handler handler, u_char *user_data, 
                                       int packet_count);
```

这些接口可以根据您的具体需求灵活使用，实现更复杂的网络数据包分析功能。