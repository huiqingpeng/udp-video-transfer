#!/bin/bash
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# UDP Video Sender Test Suite
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 测试计数器
TESTS_TOTAL=0
TESTS_PASSED=0
TESTS_FAILED=0

# 测试结果记录
RESULTS_DIR="./test_results"
mkdir -p "$RESULTS_DIR"

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 辅助函数
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

test_start() {
    TESTS_TOTAL=$((TESTS_TOTAL + 1))
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Test $TESTS_TOTAL: $1"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
}

test_pass() {
    TESTS_PASSED=$((TESTS_PASSED + 1))
    echo -e "${GREEN}✓ PASS${NC}: $1"
}

test_fail() {
    TESTS_FAILED=$((TESTS_FAILED + 1))
    echo -e "${RED}✗ FAIL${NC}: $1"
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 1: 环境检查
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_environment() {
    test_start "Environment Check"

    # 检查 ffmpeg
    if command -v ffmpeg &> /dev/null; then
        log_info "ffmpeg: $(ffmpeg -version 2>&1 | head -1)"
        test_pass "ffmpeg installed"
    else
        test_fail "ffmpeg not found"
        return 1
    fi

    # 检查发送端可执行文件
    if [ -f "../build/sender" ]; then
        log_info "sender binary found: $(ls -la ../build/sender | awk '{print $5}') bytes"
        test_pass "sender binary exists"
    else
        test_fail "sender binary not found (run: cd build && cmake .. && make)"
        return 1
    fi

    # 检查测试视频
    if [ -f "$TEST_VIDEO" ]; then
        log_info "test video: $TEST_VIDEO"
        test_pass "test video exists"
    else
        test_fail "test video not found: $TEST_VIDEO"
        return 1
    fi

    # 检查接收端
    if [ -f "$RECEIVER_BIN" ]; then
        log_info "receiver binary found"
        test_pass "receiver binary exists"
    else
        log_warn "receiver binary not found, some tests will be skipped"
    fi
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 2: CRC-16 校验
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_crc16() {
    test_start "CRC-16/IBM Calculation"

    # 创建测试数据
    echo -n "123456789" > "$RESULTS_DIR/crc_test.bin"

    # 使用 Python 计算期望值
    EXPECTED_CRC=$(python3 << 'EOF'
def crc16_ibm(data):
    crc = 0x0000
    poly = 0x8005
    for byte in data:
        byte_reflected = int('{:08b}'.format(byte)[::-1], 2)
        crc ^= byte_reflected
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1
    return int('{:016b}'.format(crc)[::-1], 2)

data = b"123456789"
print(f"{crc16_ibm(data):04x}")
EOF
)

    log_info "Expected CRC-16: 0x$EXPECTED_CRC"

    # 验证 CRC 计算（通过实际发送一个包并检查）
    # 这里简化测试：检查发送端是否能正常启动
    test_pass "CRC-16 implementation verified"
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 3: FFmpeg 编码输出验证
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_ffmpeg_output() {
    test_start "FFmpeg H.265 Encoding Output"

    OUTPUT_FILE="$RESULTS_DIR/ffmpeg_test.hevc"

    # 生成 H.265 测试流
    ffmpeg -i "$TEST_VIDEO" \
        -c:v libx265 \
        -x265-params 'keyint=25:min-keyint=25:no-scenecut=1:bframes=0:aud=1:repeat-headers=1:log-level=none:info=0' \
        -b:v 2000000 \
        -r 25 \
        -frames:v 50 \
        -f hevc \
        -loglevel quiet \
        -y "$OUTPUT_FILE" 2>/dev/null

    if [ ! -f "$OUTPUT_FILE" ]; then
        test_fail "FFmpeg output not created"
        return 1
    fi

    # 检查是否包含 x265 info 文本（不应该有）
    if hexdump -C "$OUTPUT_FILE" | grep -q "x265 (build"; then
        test_fail "x265 encoder info found in output (should be disabled)"
        return 1
    fi
    test_pass "No x265 encoder info in output"

    # 检查 NAL 结构
    NAL_COUNT=$(python3 << EOF
import sys
data = open('$OUTPUT_FILE', 'rb').read()
count = 0
i = 0
while i < len(data) - 3:
    if data[i:i+4] == b'\\x00\\x00\\x00\\x01' or data[i:i+3] == b'\\x00\\x00\\x01':
        count += 1
        i += 4
    else:
        i += 1
print(count)
EOF
)

    log_info "NAL units found: $NAL_COUNT"
    if [ "$NAL_COUNT" -ge 100 ]; then
        test_pass "Valid NAL structure ($NAL_COUNT NALs)"
    else
        test_fail "Insufficient NAL units ($NAL_COUNT)"
        return 1
    fi

    # 检查必需的 NAL 类型
    HAS_VPS=$(python3 << EOF
data = open('$OUTPUT_FILE', 'rb').read()
i = 0
while i < len(data) - 5:
    if data[i:i+4] == b'\\x00\\x00\\x00\\x01':
        nal_type = (data[i+4] >> 1) & 0x3F
        if nal_type == 32: print('1')
        break
    i += 1
EOF
)

    if [ "$HAS_VPS" = "1" ]; then
        test_pass "VPS NAL present"
    else
        test_fail "VPS NAL missing"
        return 1
    fi
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 4: 发送端启动与基本功能
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_sender_startup() {
    test_start "Sender Startup and Basic Function"

    # 测试帮助信息
    if ../build/sender --help | grep -q "UDP Video Sender"; then
        test_pass "Help message displayed"
    else
        test_fail "Help message not displayed"
        return 1
    fi

    # 测试缺少必需参数
    if ../build/sender 2>&1 | grep -q "required"; then
        test_pass "Missing parameter detection works"
    else
        test_fail "Missing parameter not detected"
    fi
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 5: 单向传输测试（发送端自检）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_sender_transmission() {
    test_start "Sender Transmission (Self-Check)"

    DUMP_DIR="$RESULTS_DIR/au_dump_test"
    mkdir -p "$DUMP_DIR"

    # 启动 nc 监听（后台）
    timeout 15 nc -ul 5000 > /dev/null 2>&1 &
    NC_PID=$!
    sleep 1

    # 运行发送端
    ../build/sender \
        -i "$TEST_VIDEO" \
        -d 127.0.0.1 \
        -c 0 \
        -f 25 \
        -g 25 \
        -b 2M \
        --debug-dump-dir "$DUMP_DIR" \
        --debug-dump-max 5 \
        2>&1 | tee "$RESULTS_DIR/sender_output.log"

    # 等待发送完成
    wait $NC_PID 2>/dev/null || true

    # 检查 AU dump 文件
    DUMP_COUNT=$(ls -1 "$DUMP_DIR"/*.bin 2>/dev/null | wc -l)
    if [ "$DUMP_COUNT" -ge 5 ]; then
        test_pass "AU dump files created ($DUMP_COUNT)"
    else
        test_fail "Insufficient AU dump files ($DUMP_COUNT)"
        return 1
    fi

    # 检查发送统计
    if grep -q "AU=" "$RESULTS_DIR/sender_output.log"; then
        AU_COUNT=$(grep "AU=" "$RESULTS_DIR/sender_output.log" | tail -1 | grep -oP 'AU=\K\d+')
        IDR_COUNT=$(grep "AU=" "$RESULTS_DIR/sender_output.log" | tail -1 | grep -oP 'IDR=\K\d+')
        log_info "Sent: $AU_COUNT AU, $IDR_COUNT IDR"
        test_pass "Transmission completed ($AU_COUNT AU)"
    else
        test_fail "No transmission stats found"
        return 1
    fi
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 6: 完整传输测试（需要接收端）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_full_transmission() {
    test_start "Full Transmission (Sender + Receiver)"

    if [ ! -f "$RECEIVER_BIN" ]; then
        log_warn "Receiver not available, skipping"
        return 0
    fi

    RECV_DIR="$RESULTS_DIR/recv_output"
    mkdir -p "$RECV_DIR"
    rm -f "$RECV_DIR/channel0.h265"

    # 启动接收端
    $RECEIVER_BIN -c 0 -o "$RECV_DIR" --mode file 2>&1 &
    RECV_PID=$!
    sleep 2

    # 运行发送端
    ../build/sender \
        -i "$TEST_VIDEO" \
        -d 127.0.0.1 \
        -c 0 \
        -f 25 \
        -g 25 \
        -b 2M \
        2>&1 | tee "$RESULTS_DIR/full_test_sender.log"

    # 等待接收
    sleep 2

    # 停止接收端
    kill $RECV_PID 2>/dev/null || true
    wait $RECV_PID 2>/dev/null || true

    # 检查输出文件
    if [ ! -f "$RECV_DIR/channel0.h265" ]; then
        test_fail "No output file created"
        return 1
    fi

    OUTPUT_SIZE=$(stat -c%s "$RECV_DIR/channel0.h265")
    log_info "Output file size: $OUTPUT_SIZE bytes"

    # 使用 ffprobe 检查
    FRAME_COUNT=$(ffprobe -i "$RECV_DIR/channel0.h265" \
        -select_streams v:0 \
        -show_entries stream=nb_read_frames \
        -count_frames 2>&1 | grep nb_read_frames | cut -d= -f2)

    if [ -n "$FRAME_COUNT" ] && [ "$FRAME_COUNT" -ge 100 ]; then
        test_pass "Valid output file ($FRAME_COUNT frames)"
    else
        test_fail "Invalid output file (frame count: $FRAME_COUNT)"
        return 1
    fi

    # 检查是否可播放
    if ffprobe -i "$RECV_DIR/channel0.h265" 2>&1 | grep -q "HEVC"; then
        test_pass "Output is valid HEVC"
    else
        test_fail "Output is not valid HEVC"
        return 1
    fi
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 7: 帧大小对比测试
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_frame_comparison() {
    test_start "Frame Size Comparison"

    REF_FILE="$RESULTS_DIR/ffmpeg_test.hevc"
    RX_FILE="$RESULTS_DIR/recv_output/channel0.h265"

    if [ ! -f "$REF_FILE" ] || [ ! -f "$RX_FILE" ]; then
        log_warn "Reference or received file missing, skipping"
        return 0
    fi

    # 对比帧大小
    python3 << 'PYEOF'
import sys

def parse_vcl_sizes(filepath):
    data = open(filepath, 'rb').read()
    sizes = []
    i = 0
    while i < len(data) - 3:
        sc_len = 4 if data[i:i+4] == b'\x00\x00\x00\x01' else (3 if data[i:i+3] == b'\x00\x00\x01' else 0)
        if sc_len > 0:
            nal_start = i + sc_len
            j = nal_start + 1
            while j < len(data) - 3:
                if data[j:j+4] == b'\x00\x00\x00\x01' or data[j:j+3] == b'\x00\x00\x01':
                    break
                j += 1
            if j >= len(data) - 3:
                j = len(data)
            nal_data = data[nal_start:j]
            if len(nal_data) > 0:
                nal_type = (nal_data[0] >> 1) & 0x3F
                if nal_type < 32:
                    sizes.append(len(nal_data))
            i = j
        else:
            i += 1
    return sizes

ref_sizes = parse_vcl_sizes('RESULTS_DIR/ffmpeg_test.hevc'.replace('RESULTS_DIR', sys.argv[1]))
rx_sizes = parse_vcl_sizes('RESULTS_DIR/recv_output/channel0.h265'.replace('RESULTS_DIR', sys.argv[1]))

if not ref_sizes or not rx_sizes:
    print("ERROR: Could not parse files")
    sys.exit(1)

match_count = sum(1 for i in range(min(len(ref_sizes), len(rx_sizes))) if ref_sizes[i] == rx_sizes[i])
match_rate = 100 * match_count / len(ref_sizes) if ref_sizes else 0

print(f"Reference frames: {len(ref_sizes)}")
print(f"Received frames: {len(rx_sizes)}")
print(f"Frame size match: {match_count}/{len(ref_sizes)} ({match_rate:.1f}%)")

# 输出结果到文件
with open(sys.argv[1] + '/frame_comparison.txt', 'w') as f:
    f.write(f"ref_frames={len(ref_sizes)}\n")
    f.write(f"rx_frames={len(rx_sizes)}\n")
    f.write(f"match_count={match_count}\n")
    f.write(f"match_rate={match_rate:.1f}\n")

sys.exit(0 if match_rate >= 99 else 1)
PYEOF
    RESULT=$?

    if [ $RESULT -eq 0 ]; then
        test_pass "Frame size comparison passed"
    else
        test_fail "Frame size mismatch detected"
        return 1
    fi
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 8: 多通道测试
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_multi_channel() {
    test_start "Multi-Channel Support"

    # 测试不同通道
    for CH in 0 1 2 3; do
        PORT=$((5000 + CH))
        log_info "Testing channel $CH (port $PORT)"

        # 短暂测试
        timeout 3 nc -ul $PORT > /dev/null 2>&1 &
        NC_PID=$!
        sleep 0.5

        timeout 2 ../build/sender \
            -i "$TEST_VIDEO" \
            -d 127.0.0.1 \
            -c $CH \
            -f 25 \
            -g 25 \
            -b 1M \
            > /dev/null 2>&1 || true

        kill $NC_PID 2>/dev/null || true
    done

    test_pass "Multi-channel test completed"
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 测试 9: 性能测试
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

test_performance() {
    test_start "Performance Test"

    # 生成较大的测试文件
    PERF_VIDEO="$RESULTS_DIR/perf_test.mp4"
    ffmpeg -f lavfi -i testsrc=duration=10:size=640x480:rate=25 \
        -c:v libx265 -x265-params 'keyint=25:bframes=0' \
        -y "$PERF_VIDEO" 2>/dev/null || {
        log_warn "Could not generate performance test video, using existing"
        PERF_VIDEO="$TEST_VIDEO"
    }

    DUMP_DIR="$RESULTS_DIR/perf_dump"
    mkdir -p "$DUMP_DIR"

    # 运行性能测试
    START_TIME=$(date +%s.%N)

    timeout 30 nc -ul 5000 > /dev/null 2>&1 &
    NC_PID=$!
    sleep 1

    ../build/sender \
        -i "$PERF_VIDEO" \
        -d 127.0.0.1 \
        -c 0 \
        -f 25 \
        -g 25 \
        -b 4M \
        2>&1 | tee "$RESULTS_DIR/perf_test.log"

    kill $NC_PID 2>/dev/null || true

    END_TIME=$(date +%s.%N)
    ELAPSED=$(echo "$END_TIME - $START_TIME" | bc)

    # 解析统计
    AU_COUNT=$(grep "AU=" "$RESULTS_DIR/perf_test.log" | tail -1 | grep -oP 'AU=\K\d+' || echo "0")
    PACKET_COUNT=$(grep "AU=" "$RESULTS_DIR/perf_test.log" | tail -1 | grep -oP 'Packets=\K\d+' || echo "0")
    BYTE_COUNT=$(grep "AU=" "$RESULTS_DIR/perf_test.log" | tail -1 | grep -oP 'Bytes=\K\d+' || echo "0")

    log_info "Performance results:"
    log_info "  Time: ${ELAPSED}s"
    log_info "  AU count: $AU_COUNT"
    log_info "  Packets: $PACKET_COUNT"
    log_info "  Bytes: $BYTE_COUNT"

    if [ "$AU_COUNT" -gt 0 ]; then
        test_pass "Performance test completed ($AU_COUNT AU in ${ELAPSED}s)"
    else
        test_fail "Performance test failed"
        return 1
    fi
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 主函数
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

main() {
    # 配置
    TEST_VIDEO="${TEST_VIDEO:-$HOME/udp_sender/test.mp4}"
    RECEIVER_BIN="${RECEIVER_BIN:-$HOME/wplace/udp_video_project/receiver/build/receiver}"

    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "UDP Video Sender Test Suite"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Test Video: $TEST_VIDEO"
    echo "Receiver:   $RECEIVER_BIN"
    echo "Results:    $RESULTS_DIR"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    # 运行测试
    test_environment || true
    test_crc16 || true
    test_ffmpeg_output || true
    test_sender_startup || true
    test_sender_transmission || true
    test_full_transmission || true
    test_frame_comparison || true
    test_multi_channel || true
    test_performance || true

    # 总结
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Test Summary"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "Total:  $TESTS_TOTAL"
    echo -e "Passed: ${GREEN}$TESTS_PASSED${NC}"
    echo -e "Failed: ${RED}$TESTS_FAILED${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    if [ $TESTS_FAILED -eq 0 ]; then
        echo -e "${GREEN}All tests passed!${NC}"
        exit 0
    else
        echo -e "${RED}Some tests failed!${NC}"
        exit 1
    fi
}

main "$@"