# UDP Video Sender 测试

## 测试结构

```
tests/
├── test_unit.cpp    # 单元测试（C++）
├── run_tests.sh     # 集成测试脚本（Bash）
└── CMakeLists.txt   # 测试构建配置
```

## 单元测试

测试核心组件：

### CRC-16 测试
- `crc16_basic`: 测试 "123456789" 的 CRC 值
- `crc16_empty`: 测试空数据
- `crc16_zeros`: 测试全零数据
- `crc16_consistency`: 测试两种接口一致性

### NAL 解析测试
- `nal_find_start_code_4byte`: 4字节起始码检测
- `nal_find_start_code_3byte`: 3字节起始码检测
- `nal_find_start_code_none`: 无起始码检测
- `nal_parse_single`: 单个NAL解析
- `nal_parse_multiple`: 多NAL解析
- `nal_type_name`: NAL类型名称
- `nal_is_vcl`: VCL NAL判断
- `nal_is_idr`: IDR NAL判断

### 协议头测试
- `protocol_header_size`: 头大小（30字节）
- `protocol_header_magic`: Magic 字段
- `protocol_header_version`: Version 字段
- `protocol_header_channel`: Channel 字段
- `protocol_header_frame_seq`: FrameSeq 字段
- `protocol_header_pts`: PTS 字段
- `protocol_header_crc`: CRC 校验

### 分片测试
- `fragment_single`: 单分片（<1440字节）
- `fragment_multiple`: 多分片（>1440字节）
- `fragment_total_size`: 分片大小总和验证

### AU 序列化测试
- `au_serialize_single_nal`: 单NAL序列化
- `au_serialize_multiple_nals`: 多NAL序列化

## 运行测试

### 单元测试

```bash
# 构建
cd sender_cpp/build
cmake .. && make

# 运行
./tests/test_unit

# 使用 CTest
ctest --output-on-failure
```

### 集成测试

```bash
# 设置测试视频路径
export TEST_VIDEO=/path/to/test.mp4

# 运行
./tests/run_tests.sh
```

## 测试输出

测试结果保存在 `test_results/` 目录：

- `sender_output.log`: 发送端输出
- `ffmpeg_test.hevc`: FFmpeg 直接输出
- `au_dump_test/`: AU dump 文件
- `recv_output/`: 接收端输出
- `frame_comparison.txt`: 帧对比结果

## 预期结果

- 单元测试：25/25 通过
- 集成测试：
  - 帧数匹配：100%
  - 帧大小匹配：100%
  - 输出文件可播放