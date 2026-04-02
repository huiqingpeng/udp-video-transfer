# M2 IDR 恢复状态机测试计划

## 目标

验证 IDR 恢复状态机正确工作：
1. 启动后进入 WAIT_FIRST_IDR
2. 收到合法 IDR 后切换到 RUNNING
3. AU 超时/解析失败/frame_seq 跳变后进入 WAIT_IDR
4. WAIT_IDR 中丢弃非 IDR
5. 收到合法 IDR 后恢复到 RUNNING

---

## 编译命令

```bash
cd /home/peong/wplace/udp_video_project/receiver
mkdir -p build dump && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## 运行命令

### 接收端（Jetson）

```bash
cd /home/peong/wplace/udp_video_project/receiver
./build/receiver -c 0 -o ./dump
```

### 发送端（192.168.8.214）

```bash
python3 ~/udp_sender/udp_sender.py \
    -i ~/udp_sender/test.mp4 \
    --dest-ip 192.168.8.136 \
    -c 0 --fps 25 --gop 25 --bitrate 2M
```

---

## 测试场景

### 场景 1：正常启动验证

**步骤：**
1. 启动接收端
2. 启动发送端
3. 观察日志

**预期日志：**
```
[FSM] Enter WAIT_FIRST_IDR
[INFO] Listening on 0.0.0.0:5000 (channel 0)
[INFO] Receiver started
[FSM] Recover to RUNNING from WAIT_IDR (IDR frame_seq=0 IRAP_type=20)
[INFO] IDR accepted: frame_seq=0 size=4105
```

**预期计数器：**
```
FSM: wait_first_idr_drops=0 wait_idr_drops=0 entries=0 recovers=0
FSM: frame_seq_gap=0 invalid_idr=0 idr_accepted>0
```

**通过标准：**
- [ ] 日志显示 Enter WAIT_FIRST_IDR
- [ ] 日志显示 Recover to RUNNING
- [ ] idr_accepted > 0
- [ ] 输出文件可播放

---

### 场景 2：AU 超时触发 WAIT_IDR

**步骤：**
1. 启动接收端
2. 启动发送端运行 5 秒
3. 停止发送端（Ctrl+C）
4. 等待 100ms（让 AU 超时）
5. 重新启动发送端

**预期日志：**
```
[WARN] AU timeout: 1 AU(s) expired
[FSM] Enter WAIT_IDR (reason: AU timeout)
[FSM] Recover to RUNNING from WAIT_IDR (IDR frame_seq=xxx IRAP_type=xx)
```

**预期计数器：**
```
au_timeout>0
wait_idr_entries>0
wait_idr_recovers>0
```

**通过标准：**
- [ ] 日志显示 AU timeout
- [ ] 日志显示 Enter WAIT_IDR (reason: AU timeout)
- [ ] 日志显示 Recover to RUNNING
- [ ] wait_idr_entries > 0
- [ ] wait_idr_recovers > 0

---

### 场景 3：frame_seq 跳变触发 WAIT_IDR

**手动验证方式：**

在发送端代码中临时添加丢帧逻辑：
```python
# 在 udp_sender.py 的 send_au 方法中添加
if self.frame_seq == 100:  # 丢弃 frame_seq=100 的 AU
    self.log.info(f"DEBUG: Dropping frame_seq={self.frame_seq}")
    self.frame_seq += 1
    return
```

**预期日志：**
```
[FSM] frame_seq gap detected: expected 100 got 101 (gap=1)
[FSM] Enter WAIT_IDR (reason: frame_seq gap)
```

**预期计数器：**
```
frame_seq_gap>0
wait_idr_entries>0
```

---

### 场景 4：WAIT_IDR 丢弃非 IDR

**步骤：**
1. 使用场景 2 或 3 的方式进入 WAIT_IDR
2. 观察日志

**预期行为：**
- WAIT_IDR 期间不输出 "[INFO] IDR accepted"
- wait_idr_drops 计数增加

**通过标准：**
- [ ] wait_idr_drops > 0（如果进入 WAIT_IDR）

---

## 验证命令

```bash
# 检查输出文件
ls -la /home/peong/wplace/udp_video_project/receiver/dump/

# 验证视频格式
ffprobe /home/peong/wplace/udp_video_project/receiver/dump/channel0.h265

# 播放视频
ffplay /home/peong/wplace/udp_video_project/receiver/dump/channel0.h265
```

---

## 统计项说明

| 统计项 | 含义 |
|--------|------|
| au_timeout | AU 重组超时数 |
| wait_first_idr_drops | WAIT_FIRST_IDR 状态丢弃的 AU |
| wait_idr_drops | WAIT_IDR 状态丢弃的 AU |
| wait_idr_entries | 进入 WAIT_IDR 的次数 |
| wait_idr_recovers | 从 WAIT_IDR 恢复的次数 |
| frame_seq_gap | frame_seq 跳变次数 |
| invalid_idr | 非法 IDR 数 |
| idr_accepted | 接受的合法 IDR 数 |

---

## 通过标准汇总

| 项目 | 标准 |
|------|------|
| 状态转换 | WAIT_FIRST_IDR -> RUNNING 正确 |
| IDR 校验 | 合法 IDR 被接受，非法 IDR 被拒绝 |
| 超时恢复 | AU 超时触发 WAIT_IDR |
| 跳变恢复 | frame_seq 跳变触发 WAIT_IDR |
| 恢复能力 | 收到合法 IDR 后恢复 RUNNING |
| 输出文件 | 可被 ffprobe/ffplay 正确识别 |

---

## 已知限制

1. frame_seq 回绕处理未实现（uint32 回绕）
2. 未实现故障注入功能（建议 M2.1 添加）
3. 统计项 wait_idr_entries 仅在 RUNNING->WAIT_IDR 时增加

---

## 测试记录

| 日期 | 场景 | 结果 | 备注 |
|------|------|------|------|
| 2026-04-01 | 正常启动 | 通过 | WAIT_FIRST_IDR -> RUNNING |
| | | | |