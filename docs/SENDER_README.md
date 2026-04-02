# UDP Video Sender 技术文档

## 概述

UDP Video Sender 是自定义 UDP 视频传输协议 v1.1 的发送端实现，用于将 H.265/HEVC 视频流通过 UDP 协议传输到接收端。

### 主要特性

- **协议 v1.1**：30 字节协议头，CRC-16/IBM 校验
- **H.265 Annex B 解析**：自动解析 NAL Unit 并组帧
- **参数集缓存**：自动注入 VPS/SPS/PPS 到 IDR 帧
- **多通道支持**：支持 4 路独立通道并发发送
- **AU Dump 调试**：用于协议层一致性验证
- **Pacing 控制**：可选的分片间延迟，减少突发丢包
- **轻量级依赖**：仅需 Python 3 + FFmpeg，无额外库依赖

### 已验证性能

| 分辨率 | 帧率 | 通道数 | 码率 | 测试结果 |
|--------|------|--------|------|----------|
| 640×480 | 25fps | 4路并发 | 2Mbps | ✅ 100% 数据完整 |
| 1600×1200 | 25fps | 4路并发 | 4Mbps | ✅ 100% 数据完整 |

---

## 协议说明

### 协议头结构（30 字节，大端序）

| 字段             | 偏移 | 大小 | 说明                        |
|------------------|------|------|-----------------------------|
| magic            | 0    | 2B   | 固定 0xAA55                 |
| version          | 2    | 1B   | 固定 0x02                   |
| channel_id       | 3    | 1B   | 通道 ID (0~3)               |
| frame_seq        | 4    | 4B   | AU 序号，每路独立递增       |
| frag_idx         | 8    | 2B   | 当前分片索引（0-based）     |
| frag_total       | 10   | 2B   | 总分片数                    |
| is_idr           | 12   | 1B   | IDR/CRA 帧标志              |
| primary_nal_type | 13   | 1B   | 首个 VCL NAL 类型           |
| au_nal_count     | 14   | 2B   | AU 内 NAL 数量              |
| pts              | 16   | 8B   | 90kHz 时间戳                |
| au_size          | 24   | 4B   | AU 序列化总字节数           |
| header_crc       | 28   | 2B   | CRC-16/IBM，覆盖前 28 字节  |

### CRC-16/IBM 参数

```
poly=0x8005, init=0x0000, refin=true, refout=true, xorout=0x0000
```

### AU 序列化格式

```
AU_payload = [4B nalu_len_be][nalu_bytes] × au_nal_count
```

- `nalu_len_be`: 4 字节大端长度
- `nalu_bytes`: NAL 数据（不含 Annex B 起始码）

### 端口映射

| 通道 | UDP 端口 |
|------|----------|
| 0    | 5000     |
| 1    | 5001     |
| 2    | 5002     |
| 3    | 5003     |

---

## 代码架构

```
sender/
├── udp_sender.py           # 主程序（单路发送）
├── multi_channel_send.sh   # 多路并发脚本
└── requirements.txt        # Python 依赖
```

### 模块结构

```
udp_sender.py
├── 协议常量定义
│   ├── PROTOCOL_MAGIC, PROTOCOL_VERSION
│   ├── HEADER_SIZE, FRAGMENT_PAYLOAD_SIZE
│   └── CHANNEL_PORT_MAP
│
├── CRC-16/IBM 实现
│   └── crc16_ibm(data: bytes) -> int
│
├── Annex B 解析
│   ├── NalUnit (dataclass)
│   ├── find_start_code(data, offset) -> (pos, len)
│   └── parse_nal_units(stream) -> List[NalUnit]
│
├── AU 组帧
│   ├── AccessUnit (dataclass)
│   ├── build_au_from_nals() -> AccessUnit
│   └── serialize_au(au) -> bytes
│
├── 协议头序列化
│   ├── ProtocolHeader (dataclass)
│   └── serialize_header(header) -> bytes
│
├── UDP 发送
│   └── build_fragments(au_payload, header) -> List[bytes]
│
├── FFmpeg 集成
│   └── build_ffmpeg_cmd(input, fps, gop, bitrate) -> List[str]
│
└── 主类 UdpSender
    ├── setup()          # 初始化 socket 和 FFmpeg
    ├── process_nal()    # NAL 处理与 AU 组帧
    ├── send_au()        # AU 序列化、分片、发送
    └── run()            # 主循环
```

---

## 核心模块详解

### 1. CRC-16/IBM 实现

```python
def crc16_ibm(data: bytes) -> int:
    """
    CRC-16/IBM 计算
    参数: poly=0x8005, init=0x0000, refin=true, refout=true, xorout=0x0000
    """
    crc = 0x0000
    poly = 0x8005

    for byte in data:
        # refin=true: 输入位反转
        byte_reflected = int('{:08b}'.format(byte)[::-1], 2)
        crc ^= byte_reflected

        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1

    # refout=true: 输出位反转（16位）
    crc_reflected = int('{:016b}'.format(crc)[::-1], 2)
    return crc_reflected
```

**关键点**：
- 输入字节需要位反转（refin=true）
- 最终 CRC 需要位反转（refout=true）
- 与接收端实现必须完全一致

### 2. Annex B 解析

Annex B 是 H.265/HEVC 的字节流格式，NAL Unit 通过起始码分隔：

```
[00 00 00 01] [NAL Unit 1] [00 00 00 01] [NAL Unit 2] ...
```

起始码有两种：
- 4 字节：`00 00 00 01`
- 3 字节：`00 00 01`

```python
def parse_nal_units(stream: bytes) -> List[NalUnit]:
    """
    解析 Annex B 字节流，提取所有 NAL Unit
    返回 NAL 列表，每个 NAL 不含起始码
    """
```

**NAL Type 解析**：
```python
nal_type = (nal_data[0] >> 1) & 0x3F
```

### 3. AU 组帧

Access Unit (AU) 是一个视频帧的所有 NAL Unit 集合。组帧规则：

1. **AUD 边界检测**：`AUD_NUT (type 35)` 作为 AU 边界
2. **参数集缓存**：VPS/SPS/PPS 需要缓存，必要时注入 IDR 帧
3. **NAL 顺序**：AUD → VPS/SPS/PPS → VCL → SEI

```python
def build_au_from_nals(nals, pts, cached_vps, cached_sps, cached_pps):
    """
    从 NAL 列表构建 AU
    检查是否为 IDR，必要时注入缓存的参数集
    """
```

### 4. 协议头序列化

```python
def serialize_header(header: ProtocolHeader) -> bytes:
    # 前 28 字节（不含 CRC）
    header_bytes = struct.pack(
        '>H B B I H H B B H Q I',  # 大端序格式
        header.magic,
        header.version,
        header.channel_id,
        header.frame_seq,
        header.frag_idx,
        header.frag_total,
        header.is_idr,
        header.primary_nal_type,
        header.au_nal_count,
        header.pts,
        header.au_size
    )

    # 计算 CRC
    crc = crc16_ibm(header_bytes)
    header_bytes += struct.pack('>H', crc)

    return header_bytes
```

### 5. 分片与发送

AU 大小通常超过 MTU，需要分片：

```python
FRAGMENT_PAYLOAD_SIZE = 1440  # 单包 payload 上限

def build_fragments(au_payload, header_template):
    au_size = len(au_payload)
    frag_total = (au_size + 1439) // 1440

    fragments = []
    for frag_idx in range(frag_total):
        offset = frag_idx * 1440
        frag_payload = au_payload[offset:offset+1440]

        # 构建完整 UDP 包
        packet = serialize_header(header) + frag_payload
        fragments.append(packet)

    return fragments
```

---

## 移植指南

### 环境要求

| 项目 | 要求 | 说明 |
|------|------|------|
| Python | 3.7+ | 无额外 pip 依赖 |
| FFmpeg | 4.0+ | 需支持 libx265 编码器 |
| 操作系统 | Linux / macOS / Windows | 跨平台兼容 |

### 依赖安装

```bash
# Ubuntu/Debian
sudo apt install ffmpeg python3

# CentOS/RHEL
sudo yum install ffmpeg python3

# macOS (Homebrew)
brew install ffmpeg python3
```

**注意**：发送端不需要安装任何 Python 第三方库，仅使用标准库。

### 文件部署

```bash
# 方式 1：直接复制
scp -r sender/ user@remote:~/udp_sender/

# 方式 2：使用 rsync（推荐）
rsync -avz sender/ user@remote:~/udp_sender/
```

### 快速验证

```bash
# 在发送端机器上测试 FFmpeg
ffmpeg -version | grep libx265

# 测试编码能力
ffmpeg -f lavfi -i testsrc=duration=1:size=640x480:rate=25 \
       -c:v libx265 -f hevc -y /dev/null
```

---

## 使用方法

### 单路发送

```bash
python3 udp_sender.py \
    -i test.mp4 \
    --dest-ip 192.168.8.136 \
    -c 0 \
    --fps 25 \
    --gop 25 \
    --bitrate 4M
```

### 多路并发发送

```bash
./multi_channel_send.sh \
    -i test.mp4 \
    --dest-ip 192.168.8.136 \
    --channels 0,1,2,3 \
    --fps 25 \
    --gop 25 \
    --bitrate 4M
```

### 参数说明

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `-i, --input` | 输入视频文件 | 必填 | `-i test.mp4` |
| `--dest-ip` | 目标 IP 地址 | 必填 | `--dest-ip 192.168.8.136` |
| `-c, --channel` | 通道 ID (0-3) | 0 | `-c 0` |
| `--fps` | 帧率 | 25 | `--fps 30` |
| `--gop` | GOP 大小（IDR 间隔帧数） | 25 | `--gop 50` |
| `--bitrate` | 码率（支持 M/K 后缀） | 4M | `--bitrate 2M` |
| `--pacing-us` | 分片间延迟（微秒） | 0 | `--pacing-us 200` |
| `--log-level` | 日志级别 | INFO | `--log-level DEBUG` |
| `--debug-dump-au-dir` | AU dump 目录 | 空 | `--debug-dump-au-dir ./dump` |
| `--debug-dump-max-au` | 最大 dump 数量 | 100 | `--debug-dump-max-au 50` |

### 使用场景示例

#### 场景 1：有线网络，高分辨率多路

```bash
# 1600×1200 @ 25fps，4路并发，4Mbps
./multi_channel_send.sh \
    -i video_1600x1200.mp4 \
    --dest-ip 192.168.8.136 \
    --channels 0,1,2,3 \
    --fps 25 --gop 25 --bitrate 4M
```

#### 场景 2：WiFi 网络，需要 pacing

```bash
# 启用 pacing 避免突发丢包
./multi_channel_send.sh \
    -i test.mp4 \
    --dest-ip 192.168.8.136 \
    --channels 0,1,2,3 \
    --bitrate 2M

# 或单路带 pacing 参数
python3 udp_sender.py -i test.mp4 --dest-ip 192.168.8.136 \
    -c 0 --bitrate 2M --pacing-us 200
```

#### 场景 3：协议调试，对比 AU 数据

发送端：
```bash
python3 udp_sender.py -i test.mp4 --dest-ip 192.168.8.136 \
    --debug-dump-au-dir ./au_dump_tx \
    --debug-dump-max-au 50
```

接收端：
```bash
./receiver -c 0 -o ./dump --mode file \
    --debug-dump-au-dir ./au_dump_rx \
    --debug-dump-max-au 50
```

对比：
```bash
# 比较 SHA256 哈希
diff <(jq -r '.[].sha256' au_dump_tx/au_manifest.json | sort) \
     <(jq -r '.[].sha256' au_dump_rx/au_manifest.json | sort)
```

---

## 性能优化

### 发送端调优

#### 1. 增大 Socket 发送缓冲区

代码中已默认设置 8MB：
```python
sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 * 1024 * 1024)
```

#### 2. 启用 Pacing（WiFi 环境）

```bash
# 建议值：100-500 微秒
python3 udp_sender.py -i test.mp4 --dest-ip 192.168.8.136 --pacing-us 200
```

#### 3. 降低码率

| 网络类型 | 建议码率/路 |
|----------|-------------|
| 千兆有线 | 4-8 Mbps |
| 百兆有线 | 2-4 Mbps |
| WiFi 5GHz | 1-2 Mbps |
| WiFi 2.4GHz | 0.5-1 Mbps |

### 系统配置

```bash
# 增大 UDP 发送缓冲区
sudo sysctl -w net.core.wmem_max=8388608
sudo sysctl -w net.core.wmem_default=4194304

# 增大网卡队列
sudo ethtool -G eth0 tx 4096 2>/dev/null || true
```

### 性能监控

发送端每 5 秒输出统计：
```
[Stats] AU=750 IDR=30 Packets=5654 Bytes=7750653 Errors=0 Seq=749 Elapsed=6.2s
```

| 字段 | 说明 |
|------|------|
| AU | 发送的 Access Unit 数量 |
| IDR | IDR 帧数量 |
| Packets | UDP 包数量 |
| Bytes | 发送字节数 |
| Errors | 发送错误数 |
| Seq | 当前帧序号 |

---

## 常见问题

### Q: 发送端日志显示成功，但接收端收不到？

排查步骤：
```bash
# 1. 检查网络连通性
ping 192.168.8.136

# 2. 检查端口是否可达
nc -zvu 192.168.8.136 5000

# 3. 检查防火墙
sudo ufw status  # Ubuntu
sudo firewall-cmd --list-all  # CentOS

# 4. 使用 tcpdump 抓包确认
sudo tcpdump -i eth0 udp port 5000 -c 10
```

### Q: 如何验证协议正确性？

使用 AU Dump 功能对比发送端和接收端数据：
```bash
# 发送端
python3 udp_sender.py ... --debug-dump-au-dir ./au_dump_tx

# 接收端
./receiver ... --debug-dump-au-dir ./au_dump_rx

# 比较哈希
sha256sum au_dump_tx/au_00000000.bin
sha256sum au_dump_rx/au_00000000.bin
```

### Q: 视频质量差怎么排查？

1. **检查统计**：接收端 `CRC_fail` 和 `AU_timeout` 是否为 0
2. **网络问题**：使用有线网络替代 WiFi
3. **降低码率**：`--bitrate 1M` 或 `--bitrate 500K`
4. **启用 pacing**：`--pacing-us 200`

### Q: FFmpeg 编码失败？

```bash
# 检查 libx265 支持
ffmpeg -encoders | grep libx265

# 测试编码
ffmpeg -f lavfi -i testsrc=duration=1:size=640x480:rate=25 \
       -c:v libx265 -f hevc -y test.hevc
```

### Q: 如何生成测试视频？

```bash
# 生成 640x480 测试视频
ffmpeg -f lavfi -i testsrc=duration=30:size=640x480:rate=25 \
       -c:v libx265 -r 25 test_640x480.mp4

# 生成 1600x1200 测试视频
ffmpeg -f lavfi -i testsrc=duration=30:size=1600x1200:rate=25 \
       -c:v libx265 -r 25 test_1600x1200.mp4
```

---

## 版本历史

### v1.1.0 (2026-04-02)

**新增功能：**
- AU Dump 调试功能（`--debug-dump-au-dir`）
- Pacing 控制参数（`--pacing-us`）
- 多路并发脚本（`multi_channel_send.sh`）

**优化改进：**
- 参数集注入逻辑优化（VPS/SPS/PPS 自动缓存和注入）
- 统计日志格式改进
- FFmpeg 参数优化（禁用版本信息 SEI）

**测试验证：**
- 640×480 @ 25fps × 4路 @ 2Mbps ✅
- 1600×1200 @ 25fps × 4路 @ 4Mbps ✅

### v1.0.0 (2026-04-01)

- 初始版本
- 支持单路和多路发送
- 完整协议 v1.1 实现

---

## 相关文档

- [协议规范](custom_udp_video_protocol_v1.1.md)
- [接收端文档](RECEIVER_README.md)
- [测试报告](m4_multi_channel_test.md)