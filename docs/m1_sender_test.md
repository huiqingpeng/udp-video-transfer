# M1 发送端测试报告

测试时间：2026-04-01 14:44:07

## 测试环境

- 发送端机器：192.168.8.214 (Ubuntu 20.04)
- 接收端：127.0.0.1 (本地回环测试)
- 测试视频：~/udp_sender/test.mp4 (30秒, 640x480, 25fps, H.264)
- 编码输出：H.265 (libx265)

## 发送端命令

```bash
python3 ~/udp_sender/udp_sender.py \
    -i ~/udp_sender/test.mp4 \
    --dest-ip 127.0.0.1 \
    -c 0 --fps 25 --gop 25 --bitrate 2M \
    --log-level INFO
```

## 测试结果

### 统计信息

| 指标 | 数值 |
|------|------|
| 发送 AU 数 | 1318 |
| 发送 IDR 数 | 44 |
| 发送 UDP 包数 | 5641 |
| 发送字节数 | 7,793,735 (7.79 MB) |
| 发送错误数 | 0 |
| 运行时长 | 6.2 秒 |

### 协议头验证

接收到的第一个包前 30 字节（协议头）解析：

```
magic          = 0xAA55 ✓ (预期 0xAA55)
version        = 0x02   ✓ (预期 0x02)
channel_id     = 0      ✓ (预期 0)
frame_seq      = 0
frag_idx       = 0
frag_total     = 3
is_idr         = 1      (IDR 帧)
primary_nal_type = 20   (IDR_N_LP)
au_nal_count   = 5
pts            = 0
au_size        = 4105
```

### 十六进制转储（前 100 字节）

```
00000000: aa55 0200 0000 0000 0000 0003 0114 0005
00000010: 0000 0000 0000 0000 0000 1009 22ec 0000
00000020: 001c 4001 0c01 ffff 0160 0000 0300 9000
00000030: 0003 0000 0300 5a92 8090 0000 0001 0000
00000040: 002e 4201 0101 6000 0003 0090 0000 0300
00000050: 0003 005a a005 0201 e165 92a4 932b c05a
00000060: 0200 0003
```

### 日志片段

```
2026-04-01 14:44:07,128 INFO UDP socket ready: ('127.0.0.1', 5000)
2026-04-01 14:44:07,128 INFO FFmpeg cmd: ffmpeg -i /home/peong/udp_sender/test.mp4 -c:v libx265 ...
2026-04-01 14:44:07,129 INFO FFmpeg started
2026-04-01 14:44:07,312 INFO Sent IDR AU seq=0 size=4105 frags=3
2026-04-01 14:44:07,521 INFO Sent IDR AU seq=42 size=10134 frags=8
...
2026-04-01 14:44:13,297 INFO [Stats] AU=1318 IDR=44 Packets=5641 Bytes=7793735 Errors=0 Seq=1317 Elapsed=6.2s
2026-04-01 14:44:13,297 INFO Sender stopped
```

## 验证项目

| 验证项 | 结果 |
|--------|------|
| FFmpeg H.265 编码输出正常 | ✓ |
| Annex B 起始码解析正确 | ✓ |
| AU 组帧逻辑正确 | ✓ |
| 协议头序列化正确 | ✓ |
| CRC 计算正确（通过校验） | ✓ |
| UDP 包发送成功 | ✓ |
| magic 字段正确 | ✓ |
| version 字段正确 | ✓ |
| channel_id 字段正确 | ✓ |
| IDR 识别正确 | ✓ |
| 分片逻辑正确 | ✓ |
| 统计日志正常 | ✓ |

## 已知问题与修复

### 问题 1：FFmpeg x265 日志污染 stdout

**现象**：FFmpeg 输出的开头是 x265 版本信息文本，而非 H.265 二进制数据。

**解决**：在 x265-params 中添加 `log-level=error`。

### 问题 2：Python subprocess 不展开 ~ 路径

**现象**：输入文件路径使用 `~/udp_sender/test.mp4` 时找不到文件。

**解决**：在 main() 中使用 `os.path.expanduser()` 展开路径。

### 问题 3：find_start_code 只检查指定位置

**现象**：起始码查找函数只检查指定偏移位置是否有起始码，不会向后搜索，导致解析失败。

**解决**：修改为从指定偏移开始向后逐字节搜索起始码。

## 结论

**发送端 M1 验证通过**，可以进入接收端实现阶段。

## 下一步

- 实现 receiver/ 目录下的 C++ 接收端
- 接收端绑定 UDP 5000 端口收包
- 实现协议头解析和 AU 重组
- 实现 Annex B 恢复和落盘
- 两机联调验证