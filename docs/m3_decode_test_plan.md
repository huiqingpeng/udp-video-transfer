# M3 实时解码验证测试计划

## 目标

验证接收端能够将重组后的 Annex B H.265 码流送入本地实时解码显示。

---

## 编译命令

```bash
cd /home/peong/wplace/udp_video_project/receiver
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j$(nproc)
```

---

## 运行命令

### 接收端（Jetson）

```bash
cd /home/peong/wplace/udp_video_project/receiver

# 仅落盘（默认模式）
./build/receiver -c 0 -o ./dump --mode file

# 仅解码显示
./build/receiver -c 0 --mode decode

# 双模式（落盘 + 解码显示）
./build/receiver -c 0 -o ./dump --mode dual
```

### 发送端（192.168.8.214）

```bash
sshpass -p 0 ssh peong@192.168.8.214 \
  "cd ~/udp_sender && python3 udp_sender.py -i ~/udp_sender/test.mp4 \
   --dest-ip 192.168.8.136 -c 0 --fps 25 --gop 25 --bitrate 2M"
```

---

## 验证步骤

### 1. 验证 file 模式

```bash
./build/receiver -c 0 -o ./dump --mode file
# 启动发送端后观察：
# - 落盘文件 ./dump/channel0.h265
# - ffprobe ./dump/channel0.h265 应显示 HEVC 视频信息
```

### 2. 验证 decode 模式

```bash
./build/receiver -c 0 --mode decode
# 启动发送端后观察：
# - 弹出 GStreamer 窗口显示实时画面
# - 统计日志中 frames_decoded 应递增
# - decode_errors 应保持为 0
```

### 3. 验证 dual 模式

```bash
./build/receiver -c 0 -o ./dump --mode dual
# 启动发送端后观察：
# - 弹出 GStreamer 窗口显示实时画面
# - 同时落盘文件 ./dump/channel0.h265
```

---

## 确认解码真的工作

1. **观察窗口**：decode/dual 模式应弹出 EGL 窗口显示画面
2. **统计计数器**：
   - `frames_decoded` 递增
   - `decode_errors` = 0 或接近 0
   - `decode_queue_size` 波动正常（不持续增长）
3. **性能指标**：
   - 收包与解码解耦，无阻塞
   - 延迟应在 200ms 以内

---

## 30 分钟稳定性测试

### 自动化脚本

```bash
# 接收端
cd /home/peong/wplace/udp_video_project/receiver
./build/receiver -c 0 -o ./dump --mode dual 2>&1 | tee /tmp/m3_test.log &

# 发送端（循环发送约 30 分钟）
# 假设每次发送 6 秒，需要发送 300 次
for i in {1..300}; do
  echo "=== Round $i/300 ===" >> /tmp/m3_test.log
  sshpass -p 0 ssh peong@192.168.8.214 \
    "cd ~/udp_sender && python3 udp_sender.py -i ~/udp_sender/test.mp4 \
     --dest-ip 192.168.8.136 -c 0 --fps 25 --gop 25 --bitrate 2M" >> /tmp/m3_test.log 2>&1
  sleep 1
done

# 停止接收端
pkill -f "receiver -c 0"
```

### 检查结果

```bash
# 检查解码错误
grep "decode_errors" /tmp/m3_test.log | tail -20

# 检查帧数
grep "frames_decoded" /tmp/m3_test.log | tail -20

# 检查是否有 GStreamer 错误
grep -E "\[GST\].*ERROR" /tmp/m3_test.log

# 检查协议层问题
grep -E "(CRC_fail|au_timeout|frame_seq_gap)" /tmp/m3_test.log | tail -10
```

---

## 问题定位指南

### 如何判断是"协议层问题"还是"解码链问题"

| 问题类型 | 诊断指标 |
|----------|----------|
| **协议层问题** | CRC_fail 增加、au_timeout 增加、frame_seq_gap 增加、AU_completed 停止增长 |
| **解码链问题** | decode_errors 增加、frames_decoded 停止增长，但 AU_completed 正常 |
| **网络问题** | total_packets 停止增长、bytes_received 无变化 |

### 常见问题排查

1. **无画面显示**
   - 检查 GStreamer pipeline 是否启动（日志中是否有 "[GST] Pipeline started"）
   - 检查 nveglglessink 是否可用（`gst-inspect-1.0 nveglglessink`）
   - 检查 DISPLAY 环境变量是否正确

2. **画面卡顿**
   - 检查 decode_queue_size 是否持续增长（解码跟不上）
   - 检查 CPU/内存使用率
   - 降低分辨率或码率

3. **解码错误**
   - 检查 AU 是否完整（au_timeout 增加）
   - 检查 IDR 恢复是否正常（wait_idr_entries 增加）
   - 检查 Annex B 转换是否正确

---

## 预期结果

### 统计示例

```
[Stats] Elapsed=30s Packets=13824 Valid=13824 CRC_fail=0 Ver_fail=0 Magic_fail=0
        au_size_fail=0 frag_total_fail=0 au_timeout=0
        AU_completed=720 AU_parse_fail=0 Duplicate=0
        FSM: wait_first_idr_drops=0 wait_idr_drops=0 entries=0 recovers=0
        FSM: frame_seq_gap=0 invalid_idr=0 idr_accepted=27
        Decode: frames=720 errors=0 queue=2 bytes=8456789
        Bytes_recv=19123245 Bytes_written=18234567
[FSM] State: RUNNING
```

### 通过标准

| 检查项 | 标准 |
|--------|------|
| 画面显示 | 实时画面正常显示 |
| 解码帧数 | frames_decoded 与 AU_completed 接近 |
| 解码错误 | decode_errors = 0 或极少 |
| 队列稳定 | decode_queue_size 不持续增长 |
| 协议正常 | CRC_fail=0, au_timeout=0 |
| 30 分钟稳定 | 无崩溃，无内存泄漏 |

---

## 已知限制

1. 仅支持单路 channel 0
2. 解码参数固定为 640x480@25fps
3. 无自适应分辨率/帧率
4. 未实现 display 模式的降级（若 GStreamer 失败，应自动转为 file）

---

## 测试记录

| 日期 | 模式 | 结果 | 备注 |
|------|------|------|------|
| | file | | |
| | decode | | |
| | dual | | |
| | 30分钟稳定性 | | |