# Jetson Orin Nano 接收端软件架构设计文档（协议验证版）v0.1

## 文档信息

- **文档名称**：Jetson Orin Nano 接收端软件架构设计文档（协议验证版）
- **文档版本**：v0.1
- **适用阶段**：协议联调 / 接收端验证 / 单板集成验证
- **目标平台**：Jetson Orin Nano
- **目标协议**：自定义 UDP 视频传输协议（当前按 AU 级传输方案实现）
- **定位**：用于验证“收包 → 校验 → AU 重组 → Annex B 恢复 → 硬解码/落盘 → 丢帧恢复”的完整链路，不作为最终量产架构定版

---

## 1. 背景与目标

现有协议定义要求接收端完成以下核心能力：

1. 按 4 个独立 UDP 端口接收 4 路视频流
2. 完成协议头校验、分片重组和丢包检测
3. 在 AI 解析路径中将码流恢复为可解码的 H.265 Annex B 格式
4. 在发生丢帧或重组失败后进入“等待 IDR”状态，并在下一个完整 IDR AU 到来时恢复解码
5. 各路通道相互隔离，某一路异常不得影响其他路
6. 维护错误统计、日志与验收能力

本设计文档的目标不是直接做“最终产品接收端”，而是给 Jetson Orin Nano 一套最短验证路径的软件架构，使其先成为一个：

- 可观测
- 可抓包
- 可落盘
- 可硬解码显示
- 可注入异常
- 可验证恢复机制

的协议验证接收平台。

---

## 2. 设计原则

### 2.1 核心原则

1. 协议正确性优先于性能最优
2. 先单路闭环，再扩展到 4 路并发
3. 先落盘验证 Annex B，再接实时硬解
4. 模块解耦，方便定位错误归因
5. Jetson 当前阶段承担“验证接收端”角色，不承担最终 FPGA/Host 架构职责

### 2.2 设计判断

虽然协议原始目标包含 Raw 透传路与 AI 解析路两条路径，但在 Jetson Orin Nano 上的当前重点应放在：

- 网络协议验证
- AU 级重组正确性验证
- IDR 恢复逻辑验证
- H.265 Annex B 还原正确性验证
- 本地硬解链路验证

而不应一开始就引入最终 Host/XDMA 或 AI 推理耦合。

---

## 3. 范围定义

### 3.1 本文档覆盖范围

本文档覆盖：

- Jetson Orin Nano 上的接收端用户态软件架构
- 4 路 UDP 收包模型
- AU 重组与恢复逻辑
- Annex B 恢复逻辑
- 本地硬解码验证路径
- 落盘与日志统计设计
- 模块职责划分
- 多阶段演进方案

### 3.2 本文档不覆盖范围

本文档不覆盖：

- 发送端实现
- 最终 Host 侧 XDMA 透传链路
- 最终 FPGA/PL 侧 AI 推理接口
- 最终商用 UI
- 最终量产部署方式
- Jetson 之外平台的适配

---

## 4. 总体架构定位

Jetson Orin Nano 接收端在当前阶段建议作为协议验证型接收器，分三层使用：

1. 协议接收层
   - 收包、校验、统计
2. 码流恢复层
   - AU 重组、Annex B 恢复、等待 IDR 状态机
3. 验证输出层
   - 文件落盘
   - 本地硬解显示
   - 调试接口输出

推荐不要一开始直接做“收包后立刻送硬解 + 同时做复杂 AI 后处理”，而是按阶段逐步演进。

---

## 5. 阶段化目标

### 5.1 阶段 1：协议正确性验证

目标：

- 单路 UDP 收包
- 协议头校验正确
- AU 重组正确
- Annex B 恢复正确
- 输出 `.h265` 文件
- 本地播放器/硬解可正常播放

### 5.2 阶段 2：恢复机制验证

目标：

- 支持“等待 IDR”状态机
- 支持故障注入（丢片 / 错 CRC / 错 version）
- 在下一个完整 IDR AU 处恢复正常解码
- 统计恢复耗时

### 5.3 阶段 3：实时解码验证

目标：

- 重组后不再落盘
- 直接送入本地解码链
- 验证实时性和显示稳定性

### 5.4 阶段 4：4 路并发验证

目标：

- 4 路独立收包线程
- 4 路独立重组状态
- 4 路并发日志与计数器
- 验证一路异常不影响其他路

---

## 6. 软件总体架构

### 6.1 逻辑架构图

```text
+--------------------------------------------------------------+
|                     Receiver Application                     |
|                                                              |
|  +-------------------+    +-------------------------------+  |
|  | UDP RX Threads    |    | Control / Metrics / Logging  |  |
|  | ch0..ch3          |    | CLI / HTTP / file counters   |  |
|  +---------+---------+    +---------------+---------------+  |
|            |                                  ^              |
|            v                                  |              |
|  +-------------------+                        |              |
|  | Header Validation |------------------------+              |
|  +---------+---------+                                       |
|            |                                                 |
|            v                                                 |
|  +-------------------+                                       |
|  | AU Reassembly     |  per-channel state / timeout / loss   |
|  +---------+---------+                                       |
|            |                                                 |
|            +------------------+-------------------+          |
|                               |                   |          |
|                               v                   v          |
|                    +-------------------+   +----------------+|
|                    | Raw Packet Sink   |   | AI Decode Path ||
|                    | pcap/file/debug   |   | Annex B build  ||
|                    +-------------------+   | wait-IDR FSM   ||
|                                            +--------+-------+|
|                                                     |        |
|                                                     v        |
|                                            +----------------+|
|                                            | Validation Out ||
|                                            | .h265 / decode ||
|                                            | display / dump ||
|                                            +----------------+|
+--------------------------------------------------------------+
```

### 6.2 模块边界

系统分为 7 个核心模块：

1. UDP 接收模块
2. 协议头校验模块
3. AU 重组模块
4. 恢复状态机模块
5. Annex B 恢复模块
6. 验证输出模块
7. 日志与统计模块

---

## 7. 技术选型建议

### 7.1 推荐实现语言

建议优先使用 C++17 实现主接收程序，原因：

- 更容易管理多线程与状态机
- 更适合高频内存管理与环形缓冲
- 易于后续接入 Jetson 解码组件
- 方便保留长期运行与多路并发扩展能力

### 7.2 其他建议组件

- 网络层：POSIX socket / `recvmsg()`
- 线程模型：`std::thread` 或 `pthread`
- 同步原语：无锁队列或轻量锁 + 每路独立对象
- 日志：`spdlog`
- 配置：YAML 或 JSON
- 统计输出：CLI + 文本文件 + 可选 HTTP `/metrics`
- 落盘：原始 `.h265`
- 实时解码：GStreamer 或本地解码服务适配层

当前阶段不建议把解码逻辑直接和收包线程绑死在一起。

---

## 8. 模块设计

### 8.1 UDP 接收模块

#### 职责

- 为每个通道创建独立 UDP socket
- 绑定固定端口
- 持续收包并附加接收时间戳
- 将收到的 datagram 交给协议头校验模块

#### 输入

- 网络 UDP 数据报

#### 输出

- `UdpPacket` 对象

#### 设计要求

- 每路独立 socket
- 每路独立线程
- 某一路阻塞/异常不得影响其他路
- 接收缓冲区独立配置

#### 推荐数据结构

```cpp
struct UdpPacket {
    uint8_t  channel_id_expected;
    uint16_t udp_len;
    uint64_t recv_monotonic_ns;
    std::vector<uint8_t> bytes;
};
```

---

### 8.2 协议头校验模块

#### 职责

- 解析协议头
- 校验 `magic`
- 校验 `version`
- 校验 `channel_id`
- 校验 `header_crc`
- 做基础字段边界检查
- 生成统一的 `ValidatedPacket`

#### 输出

```cpp
struct PacketHeader {
    uint16_t magic;
    uint8_t  version;
    uint8_t  channel_id;
    uint32_t frame_seq;
    uint16_t frag_idx;
    uint16_t frag_total;
    uint8_t  is_idr;
    uint8_t  primary_nal_type;
    uint16_t au_nal_count;
    uint64_t pts;
    uint32_t au_size;
    uint16_t header_crc;
};

struct ValidatedPacket {
    PacketHeader header;
    const uint8_t* payload;
    uint32_t payload_len;
    uint64_t recv_monotonic_ns;
};
```

#### 错误策略

以下任一失败立即丢弃并计数：

- `magic` 错误
- `version` 错误
- `channel_id` 不匹配
- CRC 校验失败
- 长度小于头长
- 分片字段非法

---

### 8.3 AU 重组模块

#### 职责

- 以 `(channel_id, frame_seq)` 为键管理重组上下文
- 根据 `frag_idx` 将 payload 写入正确偏移
- 维护每个 AU 的分片到达位图
- 在收齐时标记 AU 完整
- 超时清理未完成 AU
- 检测 `frame_seq` 跳变

#### 设计关键点

当前协议要求：

- 每路独立重组缓冲
- 支持丢帧检测
- 重组超时为 80ms
- 最大帧缓冲应支持大 IDR 帧
- 丢失或超时后进入等待 IDR 状态

#### 推荐数据结构

```cpp
struct AuAssemblyContext {
    uint32_t frame_seq;
    uint64_t first_frag_ns;
    uint16_t frag_total;
    uint16_t received_count;
    uint32_t au_size;
    uint64_t pts;
    bool is_idr;
    uint8_t primary_nal_type;
    uint16_t au_nal_count;

    std::vector<uint8_t> buffer;
    std::vector<uint8_t> frag_bitmap;
    bool completed;
};
```

#### 通道级状态

```cpp
struct ChannelReassemblyState {
    uint8_t channel_id;
    uint32_t last_completed_seq;
    bool has_last_completed_seq;
    std::unordered_map<uint32_t, AuAssemblyContext> inflight;
};
```

#### 行为约束

1. 同一 AU 内允许分片乱序到达
2. 重复分片必须丢弃并计数
3. 检测到明显跨 AU 跳变时，未完成 AU 必须判定丢失
4. 超时 AU 必须强制回收

---

### 8.4 恢复状态机模块

#### 目标

处理“丢帧 / 重组失败 / 解码失败”后的恢复逻辑。

#### 每路独立状态机

```text
INIT
  -> WAIT_FIRST_IDR
  -> RUNNING
  -> WAIT_IDR
```

#### 状态说明

- `INIT`：通道刚启动，尚未收到可供解码的完整 IDR AU
- `WAIT_FIRST_IDR`：只接受完整 IDR AU；非 IDR AU 一律丢弃
- `RUNNING`：正常状态，完整 AU 可进入后续路径
- `WAIT_IDR`：发生异常后进入恢复状态，只接受完整 IDR AU

#### 状态转移

```text
INIT -> WAIT_FIRST_IDR
WAIT_FIRST_IDR --完整IDR且参数集齐全--> RUNNING
RUNNING --AU丢失/超时/解析失败/解码失败--> WAIT_IDR
WAIT_IDR --完整IDR且参数集齐全--> RUNNING
```

#### 恢复规则

- 丢帧或重组超时后必须进入“等待 IDR”
- 只在完整 IDR 到来时恢复
- 恢复操作按通道独立进行
- 其他路不受影响

---

### 8.5 Annex B 恢复模块

#### 职责

- 将重组完成的 AU 按 `[4B nalu_len][nalu_bytes]` 解析
- 为每个 NAL 前补 `0x00 00 00 01`
- 构造标准 Annex B H.265 字节流

#### 输出

```cpp
struct AnnexBFrame {
    uint8_t channel_id;
    uint32_t frame_seq;
    uint64_t pts;
    bool is_idr;
    std::vector<uint8_t> bytes;
};
```

#### 行为要求

- 若 AU 内长度前缀解析失败，则整 AU 丢弃
- 若 AU 为 IDR，但缺少 VPS/SPS/PPS，则不得退出等待 IDR
- 成功构造的 Annex B 结果可选择：
  - 落盘
  - 送实时解码器
  - 同时做两者

---

### 8.6 验证输出模块

本模块是 Jetson 平台上的关键差异化设计。

#### 设计目标

不要让接收程序一开始就同时承担“实时解码 + 显示 + AI 后处理”。应该拆成可选输出模式：

##### 模式 A：落盘模式
- 输出为 `channelX.h265`
- 用于验证 AU 恢复后的码流是否可解码

##### 模式 B：实时解码模式
- 将 Annex B 结果送入本地解码链
- 用于观察实时画面

##### 模式 C：双输出模式
- 同时落盘和解码
- 便于问题追踪

#### 为什么要先落盘

因为这样最容易判断错误来源：

- 若文件本身无法播放，问题在协议恢复层
- 若文件可播、实时链路不通，问题在解码接入层
- 若单路可播、四路异常，问题在并发和资源管理层

---

## 9. 线程模型设计

### 9.1 推荐线程划分

以 4 路为目标，建议至少采用以下线程模型：

1. `RX Thread x4`
   - ch0~ch3 各一路
2. `Assembler/Validator Thread x4`
   - 每路负责校验 + 重组 + 状态机
3. `Output Thread x1~x2`
   - 文件输出线程
   - 可选实时解码投喂线程
4. `Metrics / Control Thread x1`
   - 状态查询、日志聚合、命令控制

### 9.2 推荐总结构

```text
4 x RX Thread
4 x Channel Worker
1 x File Writer
1 x Decode Feeder
1 x Metrics/Control
```

### 9.3 为什么不建议一个“大循环线程”全做完

因为协议本身要求：

- 4 路独立
- 不得互相阻塞
- 一路异常不得影响另一路

若使用单线程大循环，任何一个通道上的重组或解码抖动都可能拖慢其他路。

---

## 10. 通道内数据流

每路通道建议遵循以下流水线：

```text
UDP socket recv
   -> header parse/validate
   -> update metrics
   -> AU reassembly
   -> timeout / loss check
   -> wait-IDR FSM
   -> Annex B reconstruction
   -> output sink (file / decoder)
```

### 关键特点

- 通道间完全隔离
- 每路状态对象常驻内存
- 输出模块可共享，但输入上下文不能共享

---

## 11. 内存设计

### 11.1 内存目标

当前验证平台不以最低内存占用为首要目标，而以可稳定重组大 IDR AU 为目标。

### 11.2 分配策略

建议：

- 每路维护一个 `inflight` AU map
- 每个 AU buffer 在收到首个分片时分配
- 建议支持单 AU 至少 `256 KB`
- 超时或完成后立即归还
- 可后续改为对象池复用

### 11.3 内存池建议

进入 4 路并发阶段后，建议为以下对象引入池化：

- `UdpPacket`
- `AuAssemblyContext::buffer`
- `AnnexBFrame::bytes`

---

## 12. 日志与统计设计

### 12.1 每路最少统计项

建议至少包含：

- 收包总数
- 有效包数
- CRC 失败数
- version 错误数
- channel_id 错误数
- 非法分片数
- 重复分片数
- AU 超时数
- `frame_seq` 跳变次数
- WAIT_IDR 进入次数
- WAIT_IDR 恢复次数
- 输出 AU 数
- 输出 IDR AU 数
- 解码失败数

### 12.2 日志级别

- `INFO`：启动、配置、模式切换、IDR 恢复
- `WARNING`：CRC 错误、version 错误、channel_id 不匹配、超时、等待 IDR
- `ERROR`：内存分配失败、解码失败、内部状态错误

### 12.3 输出形式

建议同时支持：

1. 控制台日志
2. 滚动日志文件
3. 周期性文本统计快照
4. 可选 HTTP 状态接口

---

## 13. 配置设计

### 13.1 建议配置项

```yaml
receiver:
  bind_ip: "0.0.0.0"
  channels:
    - id: 0
      port: 5000
      enabled: true
    - id: 1
      port: 5001
      enabled: true
    - id: 2
      port: 5002
      enabled: true
    - id: 3
      port: 5003
      enabled: true

network:
  so_rcvbuf: 33554432
  mtu_payload_max: 1472
  fragment_payload_size: 1440

reassembly:
  au_timeout_ms: 80
  max_au_size: 262144
  max_inflight_per_channel: 8

output:
  mode: "file"
  file_dir: "./dump"
  realtime_decode: false
  write_raw_udp_dump: false

logging:
  level: "info"
  stats_interval_sec: 1
```

### 13.2 运行模式建议

- `--mode file`
- `--mode decode`
- `--mode dual`

---

## 14. 验证输出模式设计

### 14.1 文件模式

#### 作用
用于验证 Annex B 恢复结果是否正确。

#### 输出
- `channel0.h265`
- `channel1.h265`
- ...

#### 使用场景
- 单路先行验证
- 解析正确性检查
- Wireshark 与本地文件对照分析

### 14.2 实时解码模式

#### 作用
验证 Jetson 本地硬解链路。

#### 说明
不建议作为第一阶段默认模式。应在“落盘可播”之后启用。

### 14.3 双模式

#### 作用
同时满足观察与排障需求。

---

## 15. 故障注入设计

为验证协议恢复能力，接收端建议内置可选故障注入功能：

- 按概率丢弃指定通道分片
- 主动篡改 CRC 校验逻辑
- 模拟某个 `frame_seq` 缺失
- 模拟某个 AU 解析失败
- 模拟输出模块阻塞

### 目标

快速验证：

- WAIT_IDR 状态机
- 恢复耗时
- 其他通道隔离性
- 日志与计数器完整性

---

## 16. 开发阶段建议

### 16.1 里程碑 M1：单路最小接收器

功能：

- ch0 收包
- 头校验
- AU 重组
- 输出 `.h265`

通过标准：

- 单路连续 10 分钟无崩溃
- 输出文件可播放
- 无持续性解析错误

### 16.2 里程碑 M2：状态机与恢复

功能：

- WAIT_FIRST_IDR / RUNNING / WAIT_IDR
- 丢片后恢复
- 日志与统计完整

通过标准：

- 丢弃某 AU 后，进入 WAIT_IDR
- 下一个完整 IDR 后恢复正常

### 16.3 里程碑 M3：实时解码

功能：

- 直接送本地解码链
- 实时显示或统计解码成功率

通过标准：

- 连续 30 分钟稳定解码
- 无大规模解码错误

### 16.4 里程碑 M4：4 路并发

功能：

- 4 路独立线程
- 4 路计数器
- 一路异常不影响其他路

通过标准：

- 连续运行 30 分钟
- 统计满足当前验证目标

---

## 17. 与最终量产版的差异

当前 Jetson Orin Nano 验证架构与最终量产接收端之间，建议明确这些差异：

1. 当前以“验证协议闭环”为第一目标
2. 当前保留落盘模式，量产版通常不保留
3. 当前优先易观察，量产版优先低延迟低拷贝
4. 当前输出面向调试，量产版输出面向业务流水线
5. 当前 Jetson 本地可兼任观察与验证，量产版可能拆分为独立服务

---

## 18. 风险与注意事项

### 18.1 最大风险不是带宽，而是状态机错误

从你的协议场景看，总带宽不高，真正高风险的是：

- AU 边界判断错误
- 乱序/重复片处理错误
- WAIT_IDR 切换错误
- 参数集缺失判断错误
- 输出链阻塞反向影响接收线程

### 18.2 不建议的做法

以下做法不建议作为第一版：

- 4 路同时起步
- 一上来直接实时解码，不落盘
- 收包线程里直接做全部解析与解码
- 协议恢复层和显示层完全耦合
- 没有统计接口就开始长期压测

---

## 19. 最终建议的软件目录结构

```text
jetson_receiver/
  ├── CMakeLists.txt
  ├── config/
  │   └── receiver.yaml
  ├── include/
  │   ├── packet_header.hpp
  │   ├── channel_context.hpp
  │   ├── reassembly.hpp
  │   ├── idr_fsm.hpp
  │   ├── annexb_builder.hpp
  │   ├── metrics.hpp
  │   └── output_sink.hpp
  ├── src/
  │   ├── main.cpp
  │   ├── udp_receiver.cpp
  │   ├── header_parser.cpp
  │   ├── crc16.cpp
  │   ├── reassembly.cpp
  │   ├── idr_fsm.cpp
  │   ├── annexb_builder.cpp
  │   ├── file_sink.cpp
  │   ├── decode_sink.cpp
  │   ├── metrics.cpp
  │   └── config.cpp
  ├── tools/
  │   ├── replay_dump.py
  │   └── fault_inject.py
  └── test/
      ├── protocol_cases/
      └── integration/
```

---

## 20. 结论

Jetson Orin Nano 接收端当前最合适的定位，不是最终产品接收端，而是一个协议验证型接收平台。

最优推进路线应当是：

1. 单路收包
2. 协议头校验
3. AU 重组
4. Annex B 落盘
5. 本地解码验证
6. WAIT_IDR 恢复验证
7. 4 路并发验证
8. 最后再接实时解码与后处理

这条路线最适合在 Jetson Orin Nano 上快速闭环。

---
