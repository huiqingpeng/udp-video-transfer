# UDP 视频传输系统 — Claude Code 工作说明

## 项目概述

实现自定义 UDP 视频传输协议 v1.1，分为发送端和接收端：

- **发送端**：FFmpeg + Python 封装器，运行于 192.168.8.214（Ubuntu 20.04 x86）
- **接收端**：C++ 多线程接收器，运行于本机 Jetson Orin Nano

## 两端机器访问方式

### 本机（接收端 Jetson Orin Nano）
- 直接操作，编译运行 receiver/

### 远端（发送端 192.168.8.214）
- SSH 登录：`ssh peong@192.168.8.214`（密码：0）
- 推送代码：`scp -r sender/ peong@192.168.8.214:~/udp_sender/`
- 远端执行：`ssh peong@192.168.8.214 "cd ~/udp_sender && python3 udp_sender.py <args>"`
- 需要时用 sshpass 免交互：`sshpass -p 0 ssh peong@192.168.8.214 "<cmd>"`

## 协议核心约束（必须严格遵守）

### 协议头（30字节，全部大端序）
| 字段             | 偏移 | 大小 | 说明                        |
| ---------------- | ---- | ---- | --------------------------- |
| magic            | 0    | 2B   | 固定 0xAA55                 |
| version          | 2    | 1B   | 固定 0x02                   |
| channel_id       | 3    | 1B   | 0~3                         |
| frame_seq        | 4    | 4B   | AU粒度递增，每路独立        |
| frag_idx         | 8    | 2B   | 当前分片索引（0-based）     |
| frag_total       | 10   | 2B   | 总分片数 ceil(au_size/1440) |
| is_idr           | 12   | 1B   | IDR/CRA帧置1                |
| primary_nal_type | 13   | 1B   | 首个VCL NAL类型             |
| au_nal_count     | 14   | 2B   | AU内NAL数量                 |
| pts              | 16   | 8B   | 90kHz时间戳                 |
| au_size          | 24   | 4B   | AU序列化总字节数（不含头）  |
| header_crc       | 28   | 2B   | CRC-16/IBM，覆盖前28字节    |

### CRC-16/IBM 参数
- poly=0x8005, init=0x0000, refin=true, refout=true, xorout=0x0000

### AU 序列化格式
```
AU_payload = [4B nalu_len_be][nalu_bytes] × au_nal_count
```
- nalu_bytes **不含** Annex B 起始码
- frag_payload 上限 1440 字节
- offset = frag_idx × 1440

### IDR AU 规则
- 必须包含完整 VPS + SPS + PPS + IRAP VCL
- H.265 IDR NAL types: IDR_W_RADL=19, IDR_N_LP=20, CRA_NUT=21

### 通道端口映射
- channel 0 → UDP 5000
- channel 1 → UDP 5001
- channel 2 → UDP 5002
- channel 3 → UDP 5003

## 开发阶段（当前进度在下方标注）

### M1：单路最小可用链路【已完成 ✅】
- [x] 发送端：Python sender，文件输入，channel 0，目标 Jetson IP
- [x] 接收端：C++ receiver，单路 UDP 收包 + 头校验 + AU重组 + 落盘 .h265
- [x] 验证：落盘文件可被 ffplay 播放

**完成时间：2026-04-01**

### M2：IDR 恢复状态机【已完成 ✅】
- [x] 接收端状态机：WAIT_FIRST_IDR → RUNNING → WAIT_IDR
- [x] IDR 合法性检查：VPS + SPS + PPS + IRAP VCL
- [x] 安全上限：au_size <= 200KB, frag_total <= 200
- [x] AU 超时清理（80ms）
- [x] 统计日志完整（10 个新增计数器）
- [x] 故障注入功能：`--drop-once-frame-seq N --drop-once-frag-idx M`
- [x] 故障注入测试：AU 超时/frame_seq 跳变触发 WAIT_IDR
- [x] WAIT_IDR 恢复验证：收到合法 IDR 后恢复 RUNNING

**完成时间：2026-04-01 16:05**

**验证报告：** `docs/m2_receiver_test.md`

### M3：实时解码验证【已完成 ✅】
- [x] 新增输出模式：file / decode / dual
- [x] GStreamer 解码器实现：appsrc + h265parse + nvv4l2decoder + nveglglessink
- [x] 解码与收包解耦（同步推送模式）
- [x] 解码统计项：frames_decoded, decode_errors, decode_queue_size
- [x] 无显示环境自动降级（使用 fakesink）
- [x] file/decode 模式实测验证

**完成时间：2026-04-01 16:54**

**新增文件：**
- `receiver/include/gst_decoder.hpp` - GStreamer 解码器头文件
- `receiver/src/gst_decoder.cpp` - GStreamer 解码器实现

**修改文件：**
- `receiver/src/main.cpp` - 新增 --mode 参数，集成 GstDecoder
- `receiver/include/annexb_writer.hpp/cpp` - 新增 convert_to_annexb 方法
- `receiver/include/metrics.hpp` - 新增解码统计项
- `receiver/src/metrics.cpp` - 更新统计打印
- `receiver/CMakeLists.txt` - 添加 GStreamer 链接

**测试报告：** `docs/m3_receiver_test.md`

### M4：4路并发【已完成 ✅】
- [x] 接收端：4 路独立线程 + 独立 socket
- [x] 每路独立：重组、状态机、输出、统计
- [x] 新增参数：--channels 和 --decode-channel
- [x] 向后兼容单路模式
- [x] 发送端：4 路并发支持（multi_channel_send.sh）
- [x] 4 路联调验证

**M4.1 完成时间：2026-04-01 17:35**
**M4.2 完成时间：2026-04-02 09:30**

**新增文件：**
- `receiver/include/channel_receiver.hpp` - 单路接收器头文件
- `receiver/src/channel_receiver.cpp` - 单路接收器实现
- `receiver/include/multi_channel_receiver.hpp` - 多路接收器头文件
- `receiver/src/multi_channel_receiver.cpp` - 多路接收器实现
- `sender/multi_channel_send.sh` - 多路发送脚本

**修改文件：**
- `receiver/src/main.cpp` - 支持多路参数
- `receiver/include/metrics.hpp` - 添加 OutputMode 枚举
- `receiver/CMakeLists.txt` - 添加新源文件

**测试报告：** `docs/m4_receiver_multi_channel.md`, `docs/m4.2_test_result.md`

## 项目文件说明
```
docs/                          ← 需求文档，只读参考
sender/udp_sender.py           ← 发送端主程序（推送到远端运行）
sender/multi_channel_send.sh   ← 多路并发发送脚本
sender/requirements.txt        ← Python依赖
receiver/CMakeLists.txt        ← 接收端构建
receiver/include/              ← 头文件
receiver/src/                  ← 源文件
receiver/config/receiver.yaml  ← 运行时配置
```

## 构建方式

### 发送端（在远端执行）
```bash
sshpass -p 0 ssh peong@192.168.8.214 "pip3 install -r ~/udp_sender/requirements.txt"
```

### 接收端（本机 Jetson）
```bash
cd receiver && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

## 验证方式

### 检查落盘文件是否可播放
```bash
ffplay -i ./dump/channel0.h265
# 或
ffprobe -i ./dump/channel0.h265
```

### 抓包验证协议头
```bash
sudo tcpdump -i eth0 udp port 5000 -w /tmp/cap.pcap
# 用 Wireshark 打开检查字段
```

## 禁止事项

- 禁止直接发送结构体内存镜像，必须按字段序列化（大端）
- 禁止用 struct pragma pack 直接 cast 收到的 UDP 数据
- 每路 frame_seq 独立维护，不共享
- IDR AU 缺少 VPS/SPS/PPS 时接收端不得退出 WAIT_IDR 状态
- 接收端收包线程不能被解码/落盘操作阻塞

## 已知环境信息（已确认）

### 本机 Jetson Orin Nano
- OS: Ubuntu 22.04
- GCC/G++: 11.4.0
- CMake: 3.22.1
- ffplay/ffprobe: 4.4.2 (可用)
- 本机 IP: 192.168.8.136
- sshpass: 1.09 (可用)

### 远端发送端 192.168.8.214
- OS: Ubuntu 20.04
- FFmpeg: 4.2.7 (支持 libx265 H.265 编码)
- Python: 3.8.10
- pip: 20.0.2

### 网络
- 两机在同一 192.168.8.x 子网
- UDP 5000 端口连通性已验证
- MTU=1500，不支持 Jumbo Frame
```

**环境探查完成时间：2026-04-01 14:16:29**

---

## 任务拆分方式（给 Claude Code 下达指令）

Claude Code 不是一次性"做完所有事"，而是**一个任务一个任务推进**。

### 推荐的任务下达顺序

**任务 1：环境探查**
```
请检查以下环境：
1. 本机 Jetson 上：gcc --version, cmake --version, ffplay 是否存在
2. SSH 到 192.168.8.214：ffmpeg -version, python3 --version, pip3 --version
3. 测试两机之间 UDP 连通性（用 nc 发一个 UDP 包）
把结果写入 docs/env_check.md
```

**任务 2：发送端实现**
```
请根据 docs/udp_sender_requirements_ffmpeg_python_c_test_v0_1.md 和
docs/custom_udp_video_protocol_v1_1.md，实现 sender/udp_sender.py。

要求：
- 支持 -i 文件输入，--dest-ip，--channel（0-3），--fps，--gop，--bitrate
- FFmpeg 用 libx265，启用 aud=1 和 repeat-headers=1
- 实现完整的 Annex B 解析 → AU 组帧 → 协议头序列化 → UDP 发包
- CRC-16/IBM 参数：poly=0x8005, refin=True, refout=True
- 包含统计日志（每5秒输出一次）
- 先只做 channel 0，单路

实现完后把代码 scp 到 192.168.8.214:~/udp_sender/，
并在远端用 test.mp4（如果没有则用 ffmpeg 生成一个测试视频）跑起来，
目标IP先填 127.0.0.1 做自发自收测试（在远端用 nc -ul 5000 验证有包收到）。
```

**任务 3：接收端 M1**
```
请根据 docs/jetson_orin_nano_receiver_architecture_v0_1.md 和
docs/custom_udp_video_protocol_v1_1.md，实现 receiver/ 目录下的 C++ 接收端。

M1 范围（单路，落盘）：
- 绑定 UDP 5000 端口收包
- 协议头解析和校验（magic/version/channel_id/CRC）
- AU 重组（按 frag_idx*1440 偏移写入，80ms 超时）
- Annex B 恢复（每个 NAL 前补 0x00000001）
- 落盘到 ./dump/channel0.h265
- 统计日志

构建系统用 CMake，C++17，编译后运行验证。
不需要实现状态机、多路、实时解码——M1 只要落盘可播就算通过。
```

**任务 4：联调验证**
```
发送端已在 192.168.8.214 就绪，接收端已在本机编译好。
请执行以下联调步骤：
1. 在本机启动接收端（./build/receiver --channel 0 --output-dir ./dump）
2. 在远端启动发送端，目标 IP 填 <本机Jetson IP>，发送 channel 0
3. 等待 30 秒后停止，检查 ./dump/channel0.h265 是否存在且可用 ffplay 播放
4. 把联调结果（接收端日志、统计数据）写入 docs/m1_test_result.md
5. 更新 CLAUDE.md 中 M1 的完成状态
```

**任务 5：IDR 恢复状态机**
```
在接收端现有基础上，增加 M2 功能：
实现 INIT → WAIT_FIRST_IDR → RUNNING → WAIT_IDR 状态机...
（M1 完成后再给这个任务）