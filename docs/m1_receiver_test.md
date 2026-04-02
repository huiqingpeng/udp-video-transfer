# M1 接收端联调验收文档

测试日期：2026-04-01

---

## 1. 测试环境

| 角色 | 机器 | IP 地址 | OS | 关键软件 |
|------|------|---------|-----|---------|
| 发送端 | x86_64 | 192.168.8.214 | Ubuntu 20.04 | FFmpeg 4.2.7, Python 3.8.10 |
| 接收端 | Jetson Orin Nano | 192.168.8.136 | Ubuntu 22.04 | GCC 11.4.0, ffprobe 4.4.2 |

### 网络环境
- 两机在同一 192.168.8.x 子网
- UDP 5000 端口连通性已验证
- MTU = 1500

---

## 2. 启动命令

### 2.1 接收端（Jetson）

```bash
# 进入项目目录
cd /home/peong/wplace/udp_video_project/receiver

# 编译（首次运行）
mkdir -p build dump && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
cd ..

# 启动接收端
./build/receiver -c 0 -o ./dump
```

**参数说明：**
- `-c 0`：监听 channel 0（对应 UDP 5000 端口）
- `-o ./dump`：输出目录

### 2.2 发送端（192.168.8.214）

```bash
python3 ~/udp_sender/udp_sender.py \
    -i ~/udp_sender/test.mp4 \
    --dest-ip 192.168.8.136 \
    -c 0 \
    --fps 25 \
    --gop 25 \
    --bitrate 2M \
    --log-level INFO
```

**参数说明：**
- `-i`：输入视频文件
- `--dest-ip`：接收端 IP（Jetson）
- `-c 0`：channel 0
- `--fps 25`：帧率 25fps
- `--gop 25`：GOP 大小 25 帧
- `--bitrate 2M`：码率 2Mbps

---

## 3. 验证步骤

### 3.1 检查 UDP 包是否收到

**方法一：接收端日志**
```
[INFO] Listening on 0.0.0.0:5000 (channel 0)
[INFO] Receiver started
[Stats] Elapsed=5s Packets=2258 Valid=2258 ...
```

**方法二：抓包验证**
```bash
# 在接收端抓包
sudo tcpdump -i any udp port 5000 -c 10
```

### 3.2 检查协议头是否通过

观察接收端统计：
```
CRC_fail=0      # CRC 校验失败数
Ver_fail=0      # version 错误数
Magic_fail=0    # magic 错误数
```

全部为 0 表示协议头解析正确。

### 3.3 检查 AU 是否成功重组

观察接收端统计：
```
AU_completed=524    # 成功重组的 AU 数
AU_parse_fail=0     # AU 解析失败数
Duplicate=0         # 重复分片数
```

`AU_completed > 0` 且 `AU_parse_fail=0` 表示重组成功。

### 3.4 检查输出文件

```bash
ls -la ./dump/channel0.h265
```

预期输出：
```
-rw-rw-r-- 1 peong peong 7626581 Apr  1 15:07 ./dump/channel0.h265
```

文件大小 > 0 且随时间增长。

### 3.5 ffprobe/ffplay 验证

```bash
# 检查视频格式
ffprobe ./dump/channel0.h265
```

预期输出：
```
Input #0, hevc, from './dump/channel0.h265':
  Duration: N/A, bitrate: N/A
  Stream #0:0: Video: hevc (Main), yuv420p(tv), 640x480 [SAR 1:1 DAR 4:3], 25 fps
```

```bash
# 播放视频
ffplay ./dump/channel0.h265
```

应能正常播放，画面无花屏。

---

## 4. 测试结果记录

### 4.1 统计数据

| 指标 | 数值 | 说明 |
|------|------|------|
| total_packets | 2258 | 收包总数 |
| valid_packets | 2258 | 有效包数 |
| crc_fail | 0 | CRC 失败数 |
| version_fail | 0 | version 错误数 |
| magic_fail | 0 | magic 错误数 |
| au_completed | 524 | 完成 AU 数 |
| au_parse_fail | 0 | AU 解析失败数 |
| duplicate_frag | 0 | 重复分片数 |
| bytes_received | 3,119,460 | 接收字节数 |
| bytes_written | 3,050,280 | 写入字节数 |

### 4.2 协议头验证

| 字段 | 期望值 | 实际值 | 结果 |
|------|--------|--------|------|
| magic | 0xAA55 | 0xAA55 | ✓ |
| version | 0x02 | 0x02 | ✓ |
| channel_id | 0 | 0 | ✓ |
| CRC 校验 | 通过 | 通过 | ✓ |

### 4.3 输出文件验证

```bash
$ ls -la ./dump/channel0.h265
-rw-rw-r-- 1 peong peong 7626581 Apr  1 15:07 ./dump/channel0.h265

$ ffprobe ./dump/channel0.h265 2>&1 | grep Stream
  Stream #0:0: Video: hevc (Main), yuv420p(tv), 640x480 [SAR 1:1 DAR 4:3], 25 fps
```

---

## 5. 已知问题与风险

### 5.1 立即修复项（影响 M1 稳定性）

| 编号 | 问题 | 位置 | 风险 | 建议 |
|------|------|------|------|------|
| R1 | `au_size` 无上限检查 | reassembly.cpp:24 | 恶意包可导致内存耗尽 | 添加 `au_size <= 200KB` 检查 |
| R2 | `frag_total` 无上限检查 | reassembly.cpp:25 | bitmap 可无限增长 | 添加 `frag_total <= 200` 检查 |
| R3 | `cleanup_expired()` 未被调用 | main.cpp | AU 永不超时，内存泄漏 | 在主循环定期调用 |

### 5.2 可推迟到 M2 的项

| 编号 | 问题 | 说明 |
|------|------|------|
| D1 | 未实现 WAIT_IDR 状态机 | M2 任务 |
| D2 | 未实现 channel_id 过滤 | 当前仅单路，不影响 |
| D3 | `au_nal_count=0` 未检查 | 正常发送端不会产生 |
| D4 | AU map 无大小限制 | 长期运行可能累积 |
| D5 | 线程安全问题 | 当前单线程，不影响 |

---

## 6. 结论

**M1 单路最小可用链路验证通过**

### 通过标准
- [x] 发送端能正确封装并发送 H.265 码流
- [x] 接收端能正确解析协议头
- [x] CRC 校验正确
- [x] AU 重组正确
- [x] Annex B 恢复正确
- [x] 输出文件可被 ffprobe/ffplay 识别

### 遗留问题
- 3 个立即修复项建议在 M2 前修复
- 5 个可推迟项可在 M2/M3 阶段处理

### 下一步
- 进入 M2：实现 IDR 恢复状态机
- 修复 R1/R2/R3 立即修复项

---

## 附录：完整测试日志示例

### 接收端日志
```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
UDP Video Receiver M1 - Protocol v1.1 (version 0x02)
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
Channel: 0
Output:  ./dump

[INFO] Output file: ./dump/channel0.h265
[INFO] Listening on 0.0.0.0:5000 (channel 0)
[INFO] Receiver started
[INFO] Received IDR AU: frame_seq=0 size=4105
[INFO] Received IDR AU: frame_seq=42 size=4105
...
[Stats] Elapsed=5s Packets=2258 Valid=2258 CRC_fail=0 Ver_fail=0 Magic_fail=0
        AU_completed=524 AU_parse_fail=0 Duplicate=0
        Bytes_recv=3119460 Bytes_written=3050280

[INFO] Receiver stopped
```

### 发送端日志
```
2026-04-01 15:07:00 INFO UDP socket ready: ('192.168.8.136', 5000)
2026-04-01 15:07:00 INFO FFmpeg cmd: ffmpeg -i /home/peong/udp_sender/test.mp4 ...
2026-04-01 15:07:00 INFO FFmpeg started
2026-04-01 15:07:00 INFO Sent IDR AU seq=0 size=4105 frags=3
...
2026-04-01 15:07:06 INFO [Stats] AU=1318 IDR=44 Packets=5641 Bytes=7793735 Errors=0
2026-04-01 15:07:06 INFO Sender stopped
```