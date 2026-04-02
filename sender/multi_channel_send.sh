#!/bin/bash
# multi_channel_send.sh - 多路并发发送脚本
#
# 用法:
#   ./multi_channel_send.sh -i test.mp4 --dest-ip 192.168.8.136 --channels 0,1,2,3
#
# 参数:
#   -i, --input      输入视频文件
#   --dest-ip        目标 IP 地址
#   --channels       通道列表（逗号分隔，如 0,1,2,3）
#   --fps            帧率（默认 25）
#   --gop            GOP 大小（默认 25）
#   --bitrate        码率（默认 2M）
#   --log-level      日志级别（默认 INFO）

set -e

# 默认参数
INPUT=""
DEST_IP=""
CHANNELS=""
FPS=25
GOP=25
BITRATE="2M"
LOG_LEVEL="INFO"

# 解析参数
while [[ $# -gt 0 ]]; do
    case $1 in
        -i|--input)
            INPUT="$2"
            shift 2
            ;;
        --dest-ip)
            DEST_IP="$2"
            shift 2
            ;;
        --channels)
            CHANNELS="$2"
            shift 2
            ;;
        --fps)
            FPS="$2"
            shift 2
            ;;
        --gop)
            GOP="$2"
            shift 2
            ;;
        --bitrate)
            BITRATE="$2"
            shift 2
            ;;
        --log-level)
            LOG_LEVEL="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# 检查必需参数
if [ -z "$INPUT" ]; then
    echo "Error: --input is required"
    exit 1
fi
if [ -z "$DEST_IP" ]; then
    echo "Error: --dest-ip is required"
    exit 1
fi
if [ -z "$CHANNELS" ]; then
    echo "Error: --channels is required (e.g., 0,1,2,3)"
    exit 1
fi

# 获取脚本目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# 存储 PID
PIDS=()

# 清理函数
cleanup() {
    echo "Stopping all sender processes..."
    for pid in "${PIDS[@]}"; do
        kill $pid 2>/dev/null || true
    done
    wait
    echo "All processes stopped."
}

trap cleanup SIGINT SIGTERM

# 启动各路发送
echo "Starting multi-channel sender..."
echo "  Input: $INPUT"
echo "  Dest IP: $DEST_IP"
echo "  Channels: $CHANNELS"
echo "  FPS: $FPS, GOP: $GOP, Bitrate: $BITRATE"
echo ""

IFS=',' read -ra CH_ARRAY <<< "$CHANNELS"
for ch in "${CH_ARRAY[@]}"; do
    echo "Starting channel $ch..."
    python3 "$SCRIPT_DIR/udp_sender.py" \
        -i "$INPUT" \
        --dest-ip "$DEST_IP" \
        -c "$ch" \
        --fps "$FPS" \
        --gop "$GOP" \
        --bitrate "$BITRATE" \
        --log-level "$LOG_LEVEL" &
    PIDS+=($!)
done

echo ""
echo "All channels started. PIDs: ${PIDS[*]}"
echo "Press Ctrl+C to stop all channels."

# 等待所有进程
wait