# UDP Video Transfer System

自定义 UDP 视频传输协议 v1.1 实现，支持多路 H.265/HEVC 视频流的实时传输。

## 项目概述

本项目实现了一个完整的 UDP 视频传输系统，分为发送端和接收端：

- **发送端**：Python + FFmpeg，运行于 x86 Linux
- **接收端**：C++，运行于 Jetson Orin Nano 等嵌入式平台

### 主要特性

- 协议 v1.1：30 字节协议头，CRC-16/IBM 校验
- 多路并发：支持 4 路独立通道同时收发
- AU 重组：支持乱序和超时处理
- IDR 恢复状态机：自动检测丢包并恢复
- GStreamer 可选：无 GStreamer 依赖时仍可正常编译运行

### 已验证性能

| 分辨率 | 帧率 | 通道数 | 码率 | 测试结果 |
|--------|------|--------|------|----------|
| 640×480 | 25fps | 4路 | 2Mbps | ✅ 100% 数据完整 |
| 1600×1200 | 25fps | 4路 | 4Mbps | ✅ 100% 数据完整 |

## 快速开始

### 发送端

```bash
# 安装依赖（仅需 FFmpeg）
sudo apt install ffmpeg python3

# 单路发送
cd sender
python3 udp_sender.py -i test.mp4 --dest-ip 192.168.8.136 -c 0

# 多路发送
./multi_channel_send.sh -i test.mp4 --dest-ip 192.168.8.136 --channels 0,1,2,3
```

### 接收端

```bash
# 编译
cd receiver
mkdir build && cd build
cmake .. && make -j$(nproc)

# 单路接收
./receiver -c 0 -o ./dump --mode file

# 多路接收
./receiver --channels 0,1,2,3 -o ./dump --mode file
```

## 目录结构

```
udp_video_project/
├── sender/                    # 发送端
│   ├── udp_sender.py         # 主程序
│   ├── multi_channel_send.sh # 多路脚本
│   └── requirements.txt      # Python 依赖（空）
│
├── receiver/                  # 接收端
│   ├── CMakeLists.txt        # 构建配置
│   ├── include/              # 头文件
│   └── src/                  # 源文件
│
├── docs/                      # 文档
│   ├── SENDER_README.md      # 发送端详细文档
│   ├── RECEIVER_README.md    # 接收端详细文档
│   └── custom_udp_video_protocol_v1.1.md  # 协议规范
│
└── README.md                  # 本文件
```

## 协议说明

### 协议头结构（30 字节，大端序）

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| magic | 0 | 2B | 固定 0xAA55 |
| version | 2 | 1B | 固定 0x02 |
| channel_id | 3 | 1B | 通道 ID (0~3) |
| frame_seq | 4 | 4B | AU 序号 |
| frag_idx | 8 | 2B | 当前分片索引 |
| frag_total | 10 | 2B | 总分片数 |
| is_idr | 12 | 1B | IDR/CRA 标志 |
| primary_nal_type | 13 | 1B | 首个 VCL NAL 类型 |
| au_nal_count | 14 | 2B | AU 内 NAL 数量 |
| pts | 16 | 8B | 90kHz 时间戳 |
| au_size | 24 | 4B | AU 总字节数 |
| header_crc | 28 | 2B | CRC-16/IBM |

### 端口映射

| 通道 | UDP 端口 |
|------|----------|
| 0 | 5000 |
| 1 | 5001 |
| 2 | 5002 |
| 3 | 5003 |

## 环境要求

### 发送端

| 项目 | 要求 |
|------|------|
| Python | 3.7+ |
| FFmpeg | 4.0+ (支持 libx265) |
| 操作系统 | Linux / macOS / Windows |

### 接收端

| 项目 | 要求 |
|------|------|
| 编译器 | GCC 7+ (C++17) |
| CMake | 3.16+ |
| GStreamer | 1.14+ (可选，用于解码) |
| 操作系统 | Linux |

## 文档

- [发送端详细文档](docs/SENDER_README.md)
- [接收端详细文档](docs/RECEIVER_README.md)
- [协议规范](docs/custom_udp_video_protocol_v1.1.md)

## 开发阶段

- [x] M1：单路最小可用链路
- [x] M2：IDR 恢复状态机
- [x] M3：实时解码验证
- [x] M4：4路并发
- [x] M5：GStreamer 可选化

## 版本历史

- **v1.2.0** (2026-04-02)：GStreamer 可选化
- **v1.1.0** (2026-04-02)：多路并发、AU Dump
- **v1.0.0** (2026-04-01)：初始版本

## License

MIT