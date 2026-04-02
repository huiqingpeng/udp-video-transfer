# 视频传输质量问题修复记录

**日期：** 2026-04-02

## 问题描述

用户反馈在 Windows 播放器上播放接收的视频文件时，只有左上角的数字部分清晰，画面其他部分出现乱码/花屏。

## 问题定位过程

### 1. 初始排查

首先对比发送端生成的参考文件和接收端收到的文件：

```bash
# 生成参考文件
ffmpeg -i test.mp4 -c:v libx265 ... -y /tmp/ref.hevc

# 接收文件
./dump/channel0.h265
```

发现接收文件的 NAL 结构异常：
- 参考文件：750 帧，NAL 结构正确
- 接收文件：1317 帧，存在大量重复的小帧（4086/8182 bytes）

### 2. 问题根因分析

通过逐层排查，发现发送端存在三个关键 bug：

---

## Bug 1: x265 Encoder Info SEI 混入视频流

### 现象
FFmpeg 输出的 HEVC 流中嵌入了 x265 编码器版本信息：

```
00000070: 17 47 db bb 55 a4 fe 7f  c2 fc 4e 78 32 36 35 20  .G..U.....Nx265
00000080: 28 62 75 69 6c 64 20 31  37 39 29 20 2d 2032  .(build 179) - 2
```

这些文本数据（"x265 (build 179) - 3.2.1+1-..."）被当作 NAL 数据处理，导致解析混乱。

### 原因
x265 编码器默认会在输出流中插入包含版本信息的 SEI NAL 单元。

### 解决方案
在 x265 参数中添加 `info=0` 禁用此功能：

```python
x265_params = (
    f"keyint={gop}:min-keyint={gop}:no-scenecut=1:bframes=0:aud=1:repeat-headers=1:log-level=none:info=0"
)
```

**修改文件：** `sender/udp_sender.py` 第 386-391 行

---

## Bug 2: NAL 解析返回截断数据

### 现象
发送端输出的 AU 大小异常：
- 参考文件首个 IDR 帧大小：13270 bytes
- 发送端输出的 AU 大小：4097/8193 bytes

### 原因
`parse_nal_units()` 函数在 buffer 末尾会返回不完整的 NAL 数据。

当 buffer 只有 4096 字节时，13270 字节的 IDR 帧被截断成 4001 字节：

```python
# 错误的实现
if next_nal_start is None:
    # 最后一个 NAL，取到流末尾
    nal_data = stream[nal_start:]  # ← 这里返回了截断的数据！
    next_offset = len(stream)
```

模拟分析结果：
```
Offset 0: 读入 4096 bytes, 解析出 5 个 NAL:
  IDR_N_LP     size= 4001  ← 应该是 13270，被截断了！
```

### 解决方案
修改 `parse_nal_units()` 函数，只返回完整的 NAL（后面有起始码的 NAL）：

```python
def parse_nal_units(stream: bytes) -> List[NalUnit]:
    """
    只返回完整的 NAL（后面有起始码或到达流末尾）
    不完整的 NAL（在 buffer 末尾被截断）不会被返回
    """
    while offset < len(stream):
        # ... 查找起始码 ...

        if next_nal_start is None:
            # 没有找到下一个起始码，这个 NAL 可能不完整
            # 不返回它，让调用者读取更多数据
            break  # ← 关键修改：不返回不完整的 NAL

        # 找到下一个起始码，当前 NAL 是完整的
        nal_end = next_nal_start - next_sc_len
        nal_data = stream[nal_start:nal_end]
        # ...
```

**修改文件：** `sender/udp_sender.py` 第 109-161 行

---

## Bug 3: FFmpeg 结束时最后一个 NAL 未处理

### 现象
传输结束后，接收文件比参考文件少 1 帧（749 vs 750 帧）。

### 原因
主循环在 FFmpeg 输出结束后直接 `break`，buffer 中最后一个 NAL 未被处理：

```python
while self.running:
    chunk = self.ffmpeg_proc.stdout.read(chunk_size)
    if not chunk:
        self.log.info("FFmpeg finished")
        break  # ← buffer 中还有数据未处理！
```

### 解决方案
在 FFmpeg 结束时，处理 buffer 中剩余的数据：

```python
if not chunk:
    self.log.info("FFmpeg finished")

    # 处理 buffer 中剩余的完整 NAL
    if buffer:
        nals = parse_nal_units(buffer)
        # ... 处理 NAL ...

        # 处理最后一个不完整的 NAL
        if buffer:
            last_nal_start, last_sc_len = find_start_code(buffer, 0)
            if last_nal_start is not None:
                last_nal_data = buffer[last_nal_start:]
                # ... 发送最后一个 NAL ...
    break
```

**修改文件：** `sender/udp_sender.py` 第 682-720 行

---

## 验证结果

修复后的测试结果：

| 指标 | 参考文件 | 接收文件 |
|------|---------|---------|
| 文件大小 | 7,580,283 bytes | 7,581,033 bytes |
| 帧数 | 750 | 750 |
| NAL 数 | 1590 | 1589 |
| 帧大小匹配率 | - | **100%** |

所有帧大小完全匹配参考文件，视频质量恢复正常。

---

## 技术要点总结

### H.265 Annex B 流解析注意事项

1. **起始码检测**：需要同时检测 3 字节和 4 字节起始码
   - `00 00 01` (3 字节)
   - `00 00 00 01` (4 字节)

2. **流式处理**：处理实时流时，必须考虑数据边界问题
   - 不能假设一次读取包含完整的 NAL
   - 未处理的数据需要保留到下一次读取

3. **x265 编码器参数**：
   - `info=0`：禁用版本信息 SEI
   - `log-level=none`：禁用日志输出
   - `repeat-headers=1`：每个 IDR 前重复 VPS/SPS/PPS

### 调试方法

1. **生成参考文件**：直接用 FFmpeg 输出到文件，对比协议传输结果
2. **NAL 级别分析**：解析 H.265 NAL 类型、大小分布
3. **二进制对比**：使用 hexdump 检查数据完整性
4. **模拟分析**：用 Python 模拟发送端处理逻辑，定位问题

---

## 相关文件

- `sender/udp_sender.py` - 发送端主程序（已修复）
- `docs/udp_sender_requirements_ffmpeg_python_c_test_v0_1.md` - 发送端需求文档
- `docs/custom_udp_video_protocol_v1_1.md` - 协议规范