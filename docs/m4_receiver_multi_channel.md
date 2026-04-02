# M4.1 接收端 4 路并发实现报告

## 1. 实现概述

将单路接收器扩展为支持 1-4 路独立并发接收，每路拥有独立的：
- Socket 和接收线程
- AU 重组上下文
- IDR 状态机
- 输出文件
- 统计计数器

---

## 2. 新增/修改文件

### 新增文件
| 文件 | 功能 |
|------|------|
| `receiver/include/channel_receiver.hpp` | 单路接收器头文件 |
| `receiver/src/channel_receiver.cpp` | 单路接收器实现 |
| `receiver/include/multi_channel_receiver.hpp` | 多路接收器头文件 |
| `receiver/src/multi_channel_receiver.cpp` | 多路接收器实现 |

### 修改文件
| 文件 | 修改内容 |
|------|----------|
| `receiver/src/main.cpp` | 支持多路参数，重构主程序 |
| `receiver/include/metrics.hpp` | 添加 OutputMode 枚举 |
| `receiver/CMakeLists.txt` | 添加新源文件 |

---

## 3. 新增参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-c, --channel <0-3>` | 单路模式，指定单个通道 | 0 |
| `--channels <list>` | 多路模式，指定通道列表（如 0,1,2,3） | 0,1,2,3 |
| `--decode-channel <0-3>` | 指定解码通道 | 第一个启用的通道 |

---

## 4. 架构说明

### 4.1 单路模式

```
main() -> ChannelReceiver
              |
              +-- socket (UDP 5000)
              +-- ReassemblyManager
              +-- IdrFsm
              +-- AnnexBWriter
              +-- GstDecoder (可选)
              +-- Metrics
              +-- 接收线程
```

### 4.2 多路模式

```
main() -> MultiChannelReceiver
              |
              +-- ChannelReceiver[0] -- socket (UDP 5000) -- 线程 0
              +-- ChannelReceiver[1] -- socket (UDP 5001) -- 线程 1
              +-- ChannelReceiver[2] -- socket (UDP 5002) -- 线程 2
              +-- ChannelReceiver[3] -- socket (UDP 5003) -- 线程 3
              |
              +-- 统计线程 (每 5 秒打印汇总)
```

### 4.3 通道隔离

- 每个通道独立线程，互不阻塞
- 每个通道独立状态机，一路进入 WAIT_IDR 不影响其他路
- 每个通道独立统计，方便问题定位

---

## 5. 编译命令

```bash
cd /home/peong/wplace/udp_video_project/receiver
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
```

---

## 6. 运行示例

### 6.1 单路模式（向后兼容）

```bash
# channel 0，仅落盘
./build/receiver -c 0 -o ./dump --mode file

# channel 1，decode 模式
./build/receiver -c 1 --mode decode

# channel 2，dual 模式
./build/receiver -c 2 -o ./dump --mode dual
```

### 6.2 多路模式

```bash
# 所有 4 路，仅落盘
./build/receiver --channels 0,1,2,3 -o ./dump --mode file

# 通道 0 和 2
./build/receiver --channels 0,2 -o ./dump --mode file

# 通道 0,1,2,3，dual 模式，解码通道 0
./build/receiver --channels 0,1,2,3 -o ./dump --mode dual --decode-channel 0
```

---

## 7. 输出文件

| 模式 | 文件 |
|------|------|
| file | ./dump/channel0.h265 ~ channel3.h265 |
| decode | 无文件 |
| dual | ./dump/channel0.h265 ~ channel3.h265 |

---

## 8. 统计日志

### 8.1 单路统计（每 5 秒）

```
[CH0] Elapsed=5s Packets=2380 Valid=2380 CRC_fail=0
        au_timeout=0 AU_completed=552 IDR=21
        State=RUNNING
```

### 8.2 多路汇总（每 5 秒）

```
━━━ [AGGREGATE] ━━━
Channels: 4 | Elapsed: 5s
Packets: 9520 | Valid: 9520 | CRC_fail: 0
AU_completed: 2208 | AU_timeout: 0
Bytes_recv: 13520512 | Bytes_written: 12810208
━━━━━━━━━━━━━━━━━━━
```

---

## 9. 测试验证

### 9.1 单路模式

```bash
# 启动
./build/receiver -c 0 -o ./dump --mode file

# 发送端（192.168.8.214）
python3 udp_sender.py -i test.mp4 --dest-ip 192.168.8.136 -c 0

# 结果
-rw-rw-r-- 1 peong peong 7619785 Apr  1 17:29 channel0.h265
Stream #0:0: Video: hevc (Main), yuv420p(tv), 640x480, 25 fps
```

### 9.2 多路模式

```bash
# 启动（channel 0）
./build/receiver --channels 0 -o ./dump --mode file

# 结果
[MAIN] Initialized 1 channel(s)
[CH0] Listening on port 5000
```

---

## 10. M4.2 联调建议

### 10.1 发送端改造

需要修改发送端支持：
1. 启动参数 `--channels 0,1,2,3` 指定发送哪些通道
2. 每个通道独立 frame_seq
3. 可选：独立进程或单进程多线程

### 10.2 联调步骤

1. **单路验证**
   - 发送端：`-c 0`，接收端：`-c 0`
   - 验证所有通道分别工作正常

2. **双路验证**
   - 发送端：`--channels 0,1`
   - 接收端：`--channels 0,1`
   - 验证两路独立工作

3. **四路验证**
   - 发送端：`--channels 0,1,2,3`
   - 接收端：`--channels 0,1,2,3`
   - 验证四路独立工作

4. **隔离性验证**
   - 停止一路发送，确认其他路继续工作
   - 手动触发一路 WAIT_IDR，确认其他路不受影响

### 10.3 测试命令（M4.2）

```bash
# 接收端
./build/receiver --channels 0,1,2,3 -o ./dump --mode file

# 发送端（需要改造后）
sshpass -p 0 ssh peong@192.168.8.214 \
  "cd ~/udp_sender && python3 udp_sender.py -i test.mp4 \
   --dest-ip 192.168.8.136 --channels 0,1,2,3 --fps 25 --gop 25 --bitrate 2M"
```

---

## 11. 已知限制

1. 故障注入功能暂时未在多路模式实现
2. decode 模式只支持一路解码显示
3. 多路模式下没有带宽/资源限制

---

## 12. 结论

M4.1 接收端 4 路并发架构已实现：

- ✅ 独立 socket 和线程
- ✅ 独立重组和状态机
- ✅ 独立输出文件
- ✅ 独立统计日志
- ✅ 向后兼容单路模式
- ✅ 多路汇总统计

下一步 M4.2：发送端多路支持 + 联调验证。