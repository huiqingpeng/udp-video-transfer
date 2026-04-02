# UDP Video Receiver 技术文档

## 概述

UDP Video Receiver 是自定义 UDP 视频传输协议 v1.1 的接收端实现，支持多路 H.265/HEVC 视频流的并发接收、重组和输出。

### 主要特性

- **协议 v1.1**：30 字节协议头，CRC-16/IBM 校验
- **多路并发**：支持 1-4 路独立通道同时接收
- **AU 重组**：按 `frag_idx` 重组，支持乱序和超时处理
- **IDR 恢复状态机**：自动检测丢包并等待 IDR 恢复
- **实时解码**：GStreamer 硬件解码（可选，Jetson 平台推荐）
- **多输出模式**：文件落盘 / 实时解码 / 双输出
- **GStreamer 可选**：无 GStreamer 依赖时仍可正常编译运行（file 模式）

### 已验证性能

| 分辨率 | 帧率 | 通道数 | 码率 | GStreamer | 测试结果 |
|--------|------|--------|------|-----------|----------|
| 640×480 | 25fps | 4路并发 | 2Mbps | 有 | ✅ 100% 数据完整 |
| 1600×1200 | 25fps | 4路并发 | 4Mbps | 有 | ✅ 100% 数据完整 |
| 1600×1200 | 25fps | 4路并发 | 4Mbps | 无 | ✅ 100% 数据完整 |

### GStreamer 依赖说明

| 环境 | GStreamer | 支持模式 | 说明 |
|------|-----------|----------|------|
| Jetson 平台 | 推荐 | file/decode/dual | 硬件解码 nvv4l2decoder |
| 通用 Linux | 可选 | file/decode/dual | 软件解码或 VAAPI |
| 无 GStreamer | 不需要 | file | 自动降级，仅文件输出 |

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

### 安全上限

```cpp
constexpr uint32_t AU_SIZE_MAX = 200 * 1024;     // 200KB
constexpr uint16_t FRAG_TOTAL_MAX = 200;          // 200 分片
```

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
receiver/
├── CMakeLists.txt          # 构建配置
├── include/                # 头文件
│   ├── packet_header.hpp   # 协议头解析
│   ├── crc16.hpp           # CRC-16/IBM 实现
│   ├── reassembly.hpp      # AU 重组
│   ├── idr_fsm.hpp         # IDR 恢复状态机
│   ├── annexb_writer.hpp   # Annex B 写入
│   ├── gst_decoder.hpp     # GStreamer 解码器（可选）
│   ├── metrics.hpp         # 统计模块
│   ├── channel_receiver.hpp    # 单路接收器
│   └── multi_channel_receiver.hpp  # 多路接收器
└── src/                    # 源文件
    ├── main.cpp
    ├── packet_header.cpp
    ├── crc16.cpp
    ├── reassembly.cpp
    ├── idr_fsm.cpp
    ├── annexb_writer.cpp
    ├── gst_decoder.cpp     # （仅 HAS_GSTREAMER 时编译）
    ├── metrics.cpp
    ├── channel_receiver.cpp
    └── multi_channel_receiver.cpp
```

### 模块依赖关系

```
main.cpp
├── MultiChannelReceiver ──┬── ChannelReceiver[0]
│                          ├── ChannelReceiver[1]
│                          ├── ChannelReceiver[2]
│                          └── ChannelReceiver[3]
│
ChannelReceiver
├── ReassemblyManager  (AU 重组)
├── IdrFsm             (状态机)
├── AnnexBWriter       (文件写入)
├── GstDecoder         (硬件解码, 可选)
└── Metrics            (统计)
```

**注意**：`GstDecoder` 仅在编译时检测到 GStreamer 时才可用。无 GStreamer 时，该模块不会被编译。

---

## 核心模块详解

### 1. 协议头解析 (packet_header.hpp)

```cpp
struct PacketHeader {
    uint16_t magic;           // 0xAA55
    uint8_t  version;         // 0x02
    uint8_t  channel_id;      // 0~3
    uint32_t frame_seq;       // AU 序号
    uint16_t frag_idx;        // 当前分片索引
    uint16_t frag_total;      // 总分片数
    uint8_t  is_idr;          // 是否为 IDR 帧
    uint8_t  primary_nal_type;// 首个 VCL NAL 类型
    uint16_t au_nal_count;    // AU 内 NAL 数量
    uint64_t pts;             // 显示时间戳 (90kHz)
    uint32_t au_size;         // AU 序列化总长度
    uint16_t header_crc;      // 头部 CRC
};

enum class ParseResult {
    OK,                 // 解析成功
    INVALID_MAGIC,      // magic 错误
    INVALID_VERSION,    // version 错误
    INVALID_CHANNEL,    // channel_id 不匹配
    CRC_MISMATCH,       // CRC 校验失败
    INVALID_LENGTH,     // 长度不足
    INVALID_FRAGMENT,   // 分片字段非法
    INVALID_AU_SIZE,    // au_size 超限
    INVALID_FRAG_TOTAL, // frag_total 超限
};

ParseResult parse_header(const uint8_t* data, PacketHeader& header);
```

**解析流程**：
1. 检查数据长度 ≥ 30 字节
2. 解析各字段（大端序）
3. 验证 magic 和 version
4. 计算 CRC 并校验
5. 检查安全上限

### 2. AU 重组 (reassembly.hpp)

```cpp
struct AuContext {
    uint32_t frame_seq;
    uint16_t frag_total;
    uint32_t au_size;
    std::vector<uint8_t> buffer;        // 重组缓冲区
    std::vector<bool> frag_received;    // 分片接收位图
    uint16_t received_count;            // 已接收分片数
    std::chrono::steady_clock::time_point first_frag_time;
};

class ReassemblyManager {
public:
    ReassemblyResult process_fragment(
        const PacketHeader& header,
        const uint8_t* payload,
        size_t payload_len,
        std::vector<uint8_t>& completed_au
    );

    void cleanup_expired();  // 清理超时 AU
};
```

**重组逻辑**：
1. 以 `(channel_id, frame_seq)` 为键管理重组上下文
2. 按 `frag_idx × 1440` 计算偏移写入
3. 使用位图跟踪已接收分片
4. 检测完整性：`received_count == frag_total`
5. 超时清理（默认 80ms）

### 3. IDR 恢复状态机 (idr_fsm.hpp)

```
状态转换图：

    INIT
      │
      ▼
WAIT_FIRST_IDR ──收到合法 IDR──► RUNNING
      ▲                              │
      │                              │ 异常（丢包/超时）
      │                              ▼
      └───────收到合法 IDR──────── WAIT_IDR
```

```cpp
enum class FsmState {
    INIT,
    WAIT_FIRST_IDR,
    RUNNING,
    WAIT_IDR
};

class IdrFsm {
public:
    bool process_au(const std::vector<uint8_t>& au_data,
                    const PacketHeader& header,
                    bool parse_success);

    void on_au_timeout();
    bool check_frame_seq_gap(uint32_t new_seq);
};
```

**IDR 合法性检查**：
```cpp
struct IdrCheckResult {
    bool is_valid;
    bool has_vps;      // 必须有 VPS
    bool has_sps;      // 必须有 SPS
    bool has_pps;      // 必须有 PPS
    bool has_irap_vcl; // 必须有 IRAP VCL (19/20/21)
};
```

**进入 WAIT_IDR 的条件**：
- AU 重组超时
- `frame_seq` 跳变（检测丢包）
- IDR 帧参数集缺失

### 4. Annex B 写入 (annexb_writer.hpp)

将 AU 序列化格式转换为 Annex B 字节流：

```cpp
class AnnexBWriter {
public:
    // AU 格式: [4B len][NAL data] × N
    // 输出: [00 00 00 01][NAL data] × N
    bool write_au(const std::vector<uint8_t>& au_data);
};
```

**转换过程**：
1. 读取 4 字节 NAL 长度
2. 写入 4 字节起始码 `00 00 00 01`
3. 写入 NAL 数据
4. 重复直到 AU 结束

### 5. GStreamer 解码器 (gst_decoder.hpp) - 可选

> **注意**：此模块仅在编译时检测到 GStreamer 时可用。无 GStreamer 时不会编译此模块。

```cpp
class GstDecoder {
public:
    GstDecoder(int channel_id);
    bool init();
    void push(const std::vector<uint8_t>& annexb_data, uint64_t pts);
    void stop();

private:
    GstElement* pipeline_;
    GstElement* appsrc_;
    GstElement* sink_;
};
```

**Pipeline 结构**：
```
appsrc → h265parse → nvv4l2decoder → nveglglessink
                                     ↓
                                  fakesink (无显示环境)
```

**关键配置**：
- `nvv4l2decoder`：Jetson 硬件解码器
- `nveglglessink`：EGL 无窗口显示
- 自动降级到 `fakesink`（无显示环境）

**编译条件**：
```cpp
#ifdef HAS_GSTREAMER
    // GstDecoder 相关代码
#endif
```

### 6. 单路接收器 (channel_receiver.hpp)

```cpp
class ChannelReceiver {
public:
    ChannelReceiver(const ChannelConfig& config);
    bool init();
    void start();
    void stop();
    void join();

private:
    void receive_loop();
    void process_completed_au(const std::vector<uint8_t>& au_data,
                               const PacketHeader& header);
};
```

**接收循环**：
1. `recvfrom()` 接收 UDP 包
2. `parse_header()` 解析协议头
3. `reassembly_.process_fragment()` 重组
4. `fsm_.process_au()` 状态机处理
5. `writer_->write_au()` 写文件
6. `decoder_->push()` 解码显示

### 7. 多路接收器 (multi_channel_receiver.hpp)

```cpp
class MultiChannelReceiver {
public:
    bool init();
    void start();
    void stop();
    void join();

private:
    std::vector<std::unique_ptr<ChannelReceiver>> channels_;
    std::thread stats_thread_;  // 统计打印线程
};
```

**设计特点**：
- 每路独立 socket 和线程
- 每路独立重组上下文和状态机
- 统一统计汇总打印

---

## 移植指南

### 环境要求

| 项目 | 最低要求 | 推荐配置 |
|------|----------|----------|
| 操作系统 | Linux (内核 4.x+) | Ubuntu 20.04+ |
| 编译器 | GCC 7+ / Clang 8+ | GCC 9+ / Clang 10+ |
| C++ 标准 | C++17 | C++17 |
| CMake | 3.16+ | 3.20+ |
| GStreamer | 1.14+ (可选) | 1.16+ (Jetson) |

### 依赖安装

#### Jetson 平台（完整功能）

```bash
# 编译工具
sudo apt install build-essential cmake

# GStreamer 开发包（可选，用于解码）
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

# GStreamer 插件（运行时需要）
sudo apt install gstreamer1.0-plugins-bad gstreamer1.0-plugins-base \
                 gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly
```

#### 通用 Linux 平台（仅文件模式）

```bash
# 最小依赖（无 GStreamer）
sudo apt install build-essential cmake

# 编译
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 编译选项

```bash
mkdir build && cd build

# 默认：自动检测 GStreamer
cmake .. -DCMAKE_BUILD_TYPE=Release

# 强制禁用 GStreamer（用于测试或无 GPU 环境）
cmake .. -DCMAKE_BUILD_TYPE=Release -DDISABLE_GSTREAMER=ON

# 编译
make -j$(nproc)
```

**编译输出说明**：
```
-- GStreamer found, decode mode enabled        # 找到 GStreamer
-- GStreamer not found, decode mode disabled  # 未找到，仅 file 模式
```

### 交叉编译

对于 ARM 嵌入式平台：

```bash
# 设置交叉编译工具链
export CC=aarch64-linux-gnu-gcc
export CXX=aarch64-linux-gnu-g++

# 配置
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DCMAKE_TOOLCHAIN_FILE=../toolchain-aarch64.cmake

# 或指定 GStreamer 路径
cmake .. -DCMAKE_BUILD_TYPE=Release \
         -DGSTREAMER_ROOT=/path/to/arm/sysroot

make -j$(nproc)
```

### 非 Jetson 平台适配

如果目标平台有 GStreamer 但不是 Jetson，需要修改解码器 pipeline：

**Intel VAAPI**：
```cpp
// src/gst_decoder.cpp
pipeline_str = "appsrc ! h265parse ! vaapih265dec ! vaapisink";
```

**软件解码**：
```cpp
pipeline_str = "appsrc ! h265parse ! avdec_h265 ! autovideosink";
```

**无解码器**：
```bash
# 使用 file 模式
./receiver --channels 0,1,2,3 -o ./dump --mode file

# 或编译时禁用 GStreamer
cmake .. -DDISABLE_GSTREAMER=ON
```

---

## 使用方法

### 基本用法

```bash
# 单路接收（文件模式）
./receiver -c 0 -o ./dump --mode file

# 多路接收（4路并发）
./receiver --channels 0,1,2,3 -o ./dump --mode file

# 解码显示（需要 GStreamer）
./receiver -c 0 --mode decode

# 双输出（落盘 + 解码）
./receiver --channels 0,1,2,3 -o ./dump --mode dual --decode-channel 0
```

### 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-c, --channel` | 单路模式通道 ID | 0 |
| `--channels` | 多路模式通道列表 | 0,1,2,3 |
| `-o, --output` | 输出目录 | ./dump |
| `--mode` | 输出模式：file/decode/dual | file |
| `--decode-channel` | 解码通道（多路模式） | 第一个通道 |
| `--debug-dump-au-dir` | AU dump 目录 | 空 |
| `--debug-dump-max-au` | 最大 dump 数量 | 100 |

### 输出模式

| 模式 | 说明 | GStreamer 要求 |
|------|------|----------------|
| `file` | 仅落盘 H.265 文件 | 不需要 |
| `decode` | 仅实时解码显示 | 必需 |
| `dual` | 落盘 + 解码显示 | 必需 |

**无 GStreamer 时**：`decode` 和 `dual` 模式会自动降级为 `file` 模式，并输出警告。

### 使用场景示例

#### 场景 1：嵌入式平台，无 GPU，仅文件输出

```bash
# 编译时禁用 GStreamer
cmake .. -DDISABLE_GSTREAMER=ON
make -j$(nproc)

# 运行
./receiver --channels 0,1,2,3 -o ./dump --mode file
```

#### 场景 2：Jetson 平台，4路并发 + 单路解码

```bash
# 编译（自动检测 GStreamer）
cmake .. && make -j$(nproc)

# 运行：4路落盘，通道 0 同时解码显示
./receiver --channels 0,1,2,3 -o ./dump --mode dual --decode-channel 0
```

#### 场景 3：协议调试，对比 AU 数据

```bash
# 发送端
python3 udp_sender.py -i test.mp4 --dest-ip 192.168.8.136 \
    --debug-dump-au-dir ./au_dump_tx --debug-dump-max-au 50

# 接收端
./receiver -c 0 -o ./dump --mode file \
    --debug-dump-au-dir ./au_dump_rx --debug-dump-max-au 50

# 对比
diff <(sha256sum au_dump_tx/*.bin | sort) \
     <(sha256sum au_dump_rx/*.bin | sort)
```

#### 场景 4：无显示环境（服务器）

```bash
# 方式 1：使用 file 模式
./receiver --channels 0,1,2,3 -o ./dump --mode file

# 方式 2：decode 模式自动降级
# GStreamer 检测到无显示环境会使用 fakesink
./receiver -c 0 --mode decode
```

### AU Dump 调试

```bash
./receiver -c 0 -o ./dump --mode file \
    --debug-dump-au-dir ./au_dump_rx \
    --debug-dump-max-au 50
```

输出：
- `au_00000000.bin`：AU 原始数据
- `au_manifest.json`：元信息（含 SHA256）

### 故障注入测试

```bash
# 模拟丢包：丢弃 frame_seq=100 的第一个分片
./receiver -c 0 --drop-once-frame-seq 100 --drop-once-frag-idx 0
```

---

## 系统配置优化

### Socket 缓冲区

```bash
# 查看当前配置
sysctl net.core.rmem_max

# 增大接收缓冲区
sudo sysctl -w net.core.rmem_max=67108864
sudo sysctl -w net.core.rmem_default=33554432
```

### 网卡队列

```bash
# 查看网卡统计
ip -s link show eth0

# 增大网卡队列
sudo ethtool -G eth0 rx 4096
```

### Jetson 特定配置

```bash
# 启用最大性能模式
sudo nvpmodel -m 0
sudo jetson_clocks

# 检查 GPU 频率
tegrastats
```

---

## 统计输出

接收端定期打印统计信息：

```
[CH0] Elapsed=30s Packets=5654 Valid=5654 CRC_fail=0
        au_timeout=0 AU_completed=750 IDR=30
        State=RUNNING
```

| 字段 | 说明 |
|------|------|
| Packets | 收到的 UDP 包数 |
| Valid | 协议头校验通过的包数 |
| CRC_fail | CRC 校验失败数 |
| au_timeout | AU 重组超时数 |
| AU_completed | 完整重组的 AU 数 |
| IDR | 收到的 IDR 帧数 |
| State | 状态机当前状态 |

---

## 常见问题

### Q: 编译时显示 "GStreamer not found"

A: 这是正常的！GStreamer 是可选依赖：
- 如果只需要文件输出：直接继续编译，运行时使用 `--mode file`
- 如果需要解码功能：安装 GStreamer 开发包

```bash
# 安装 GStreamer（可选）
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

# 或跳过 GStreamer 继续编译
cmake .. && make -j$(nproc)
# 输出: GStreamer not found, decode mode disabled (file mode only)
```

### Q: 运行 decode 模式时提示 "GStreamer not available"

A: 表示编译时未检测到 GStreamer。解决方案：
1. 安装 GStreamer 后重新编译
2. 或使用 `--mode file` 仅做文件输出

```bash
# 方式 1：重新编译
sudo apt install libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev
rm -rf build && mkdir build && cd build
cmake .. && make -j$(nproc)

# 方式 2：使用 file 模式
./receiver --channels 0,1,2,3 -o ./dump --mode file
```

### Q: 解码失败 "Could not initialize SDL"

A: 无显示环境是正常的，两种解决方案：
```bash
# 方式 1：使用 file 模式
./receiver -c 0 -o ./dump --mode file

# 方式 2：设置 DISPLAY 环境变量（如有 X11）
DISPLAY=:0 ./receiver -c 0 --mode decode
```

### Q: 丢包严重怎么排查？

排查步骤：
```bash
# 1. 查看接收端统计
#    重点关注 CRC_fail 和 au_timeout
[CH0] Packets=5654 Valid=5654 CRC_fail=0
       au_timeout=0 AU_completed=750

# 2. 检查网卡统计
ip -s link show eth0 | grep -A1 "RX:"

# 3. 增大系统缓冲区
sudo sysctl -w net.core.rmem_max=67108864

# 4. 检查网络类型
#    有线网络 > WiFi 5GHz > WiFi 2.4GHz
```

### Q: 视频播放花屏

可能原因：
1. **IDR 帧丢失**：检查 `IDR` 计数是否与发送端一致
2. **参数集丢失**：发送端需启用 `repeat-headers=1`
3. **解码器不支持**：确认 JetPack 版本或使用软件解码

```bash
# 检查 IDR 帧数
# 发送端：IDR=30
# 接收端：IDR 应该也是 30

# 检查输出文件
ffprobe -v error -show_streams ./dump/channel0.h265
ffmpeg -v error -i ./dump/channel0.h265 -f null -
```

### Q: 无 GStreamer 环境如何验证视频？

使用 ffplay 或 ffmpeg 离线验证：
```bash
# 播放
ffplay ./dump/channel0.h265

# 验证完整性
ffmpeg -v error -i ./dump/channel0.h265 -f null -

# 转码为 mp4
ffmpeg -i ./dump/channel0.h265 -c copy output.mp4
```

---

## 性能指标

测试环境：Jetson Orin Nano

| 配置 | CPU 占用 | 内存占用 | 丢包率 | 解码延迟 |
|------|----------|----------|--------|----------|
| 640×480 @ 25fps × 1路 (decode) | ~15% | ~80MB | <0.1% | ~40ms |
| 640×480 @ 25fps × 4路 (file) | ~30% | ~200MB | <0.5% | - |
| 1600×1200 @ 25fps × 4路 (file) | ~55% | ~300MB | <0.5% | - |
| 1600×1200 @ 25fps × 4路 (无 GStreamer) | ~50% | ~280MB | <0.5% | - |

**说明**：
- CPU 占用包含收包、重组、文件写入开销
- 无 GStreamer 时内存占用略低（无需解码缓冲区）
- 丢包率受网络环境影响，有线网络优于 WiFi

---

## 版本历史

### v1.2.0 (2026-04-02)

**新增功能：**
- GStreamer 成为可选依赖
- 编译选项 `DISABLE_GSTREAMER`
- 无 GStreamer 时自动降级为 file 模式

**优化改进：**
- 简化依赖，最小仅需 CMake + GCC
- 适配更多嵌入式平台

**测试验证：**
- 1600×1200 @ 25fps × 4路 (无 GStreamer) ✅

### v1.1.0 (2026-04-02)

**新增功能：**
- 多路并发支持
- AU Dump 调试功能
- 故障注入测试

**优化改进：**
- IDR 状态机逻辑优化
- 统计输出格式改进

### v1.0.0 (2026-04-01)

- 初始版本
- 单路接收 + 解码

---

## 相关文档

- [协议规范](custom_udp_video_protocol_v1.1.md)
- [发送端文档](SENDER_README.md)
- [测试报告](m4_multi_channel_test.md)