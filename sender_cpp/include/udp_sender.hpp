/**
 * @file udp_sender.hpp
 * @brief UDP 视频发送端主类
 */

#ifndef UDP_SENDER_HPP
#define UDP_SENDER_HPP

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <atomic>

#include "h265_nal.hpp"
#include "protocol.hpp"

namespace udp_video {

/**
 * @brief 发送端配置参数
 */
struct SenderConfig {
    std::string input_file;         // 输入视频文件
    std::string dest_ip;            // 目标 IP
    uint8_t channel = 0;            // 通道 ID (0-3)
    int fps = 25;                   // 帧率
    int gop = 25;                   // GOP 大小
    std::string bitrate = "4M";     // 码率
    int pacing_us = 0;              // 分片间延迟（微秒）
    bool verbose = false;           // 详细日志

    // 调试选项
    std::string debug_dump_dir;     // AU dump 目录
    int debug_dump_max = 100;       // 最大 dump 数量
};

/**
 * @brief 发送端统计
 */
struct SenderStats {
    uint64_t au_sent = 0;
    uint64_t idr_sent = 0;
    uint64_t packets_sent = 0;
    uint64_t bytes_sent = 0;
    uint64_t send_errors = 0;
    uint32_t current_frame_seq = 0;
    uint64_t nals_parsed = 0;
    uint64_t au_parse_errors = 0;

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_print_time;

    SenderStats() {
        start_time = std::chrono::steady_clock::now();
        last_print_time = start_time;
    }

    void print_stats() const;
};

/**
 * @brief UDP 视频发送端
 */
class UdpSender {
public:
    explicit UdpSender(const SenderConfig& config);
    ~UdpSender();

    /**
     * @brief 运行发送端
     */
    void run();

    /**
     * @brief 停止发送
     */
    void stop();

private:
    /**
     * @brief 初始化（创建 socket，启动 FFmpeg）
     */
    void setup();

    /**
     * @brief 处理单个 NAL，按 AUD 边界组帧
     */
    std::optional<AccessUnit> process_nal(NalUnit& nal, uint64_t pts);

    /**
     * @brief 发送完整 AU
     */
    void send_au(const AccessUnit& au);

    /**
     * @brief 构建 FFmpeg 命令行
     */
    std::string build_ffmpeg_cmd() const;

    /**
     * @brief 解析码率字符串
     */
    int parse_bitrate() const;

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // AU dump 调试功能
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    void dump_au(const std::vector<uint8_t>& au_payload, const AccessUnit& au, uint32_t frame_seq);
    void write_manifest();

private:
    SenderConfig config_;
    SenderStats stats_;

    // 缓存的参数集
    std::vector<uint8_t> cached_vps_;
    std::vector<uint8_t> cached_sps_;
    std::vector<uint8_t> cached_pps_;

    // AU 缓存（用于组帧）
    std::vector<NalUnit> pending_nals_;
    uint64_t pending_pts_ = 0;
    uint32_t frame_seq_ = 0;

    // UDP socket
    int sock_ = -1;

    // FFmpeg 进程
    FILE* ffmpeg_pipe_ = nullptr;

    // 运行状态
    std::atomic<bool> running_{false};

    // AU dump 调试
    int debug_dump_count_ = 0;
    std::vector<std::string> debug_dump_manifest_;
};

} // namespace udp_video

#endif // UDP_SENDER_HPP