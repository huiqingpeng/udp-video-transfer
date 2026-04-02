/**
 * @file metrics.hpp
 * @brief 统计计数器和公共类型定义
 */

#ifndef METRICS_HPP
#define METRICS_HPP

#include <cstdint>
#include <atomic>
#include <string>
#include <chrono>

namespace udp_video {

/**
 * @brief 输出模式
 */
enum class OutputMode {
    FILE,     // 仅落盘
    DECODE,   // 仅解码显示
    DUAL      // 落盘 + 解码显示
};

/**
 * @brief 接收端统计计数器
 */
struct Metrics {
    // 收包统计
    std::atomic<uint64_t> total_packets{0};       // 收包总数
    std::atomic<uint64_t> valid_packets{0};       // 有效包数
    std::atomic<uint64_t> crc_fail{0};            // CRC 失败数
    std::atomic<uint64_t> version_fail{0};        // version 错误数
    std::atomic<uint64_t> magic_fail{0};          // magic 错误数
    std::atomic<uint64_t> channel_mismatch{0};    // channel 不匹配数
    std::atomic<uint64_t> invalid_fragment{0};    // 非法分片数
    std::atomic<uint64_t> invalid_au_size{0};     // au_size 超限数
    std::atomic<uint64_t> invalid_frag_total{0};  // frag_total 超限数
    std::atomic<uint64_t> duplicate_frag{0};      // 重复分片数

    // AU 统计
    std::atomic<uint64_t> au_completed{0};        // 完成 AU 数
    std::atomic<uint64_t> au_parse_fail{0};       // AU 解析失败数
    std::atomic<uint64_t> au_timeout{0};          // AU 超时数
    std::atomic<uint64_t> bytes_written{0};       // 写入字节数
    std::atomic<uint64_t> bytes_received{0};      // 接收字节数

    // M2 状态机统计
    std::atomic<uint64_t> wait_first_idr_drops{0};  // WAIT_FIRST_IDR 丢弃数
    std::atomic<uint64_t> wait_idr_drops{0};        // WAIT_IDR 丢弃数
    std::atomic<uint64_t> wait_idr_entries{0};      // 进入 WAIT_IDR 次数
    std::atomic<uint64_t> wait_idr_recovers{0};     // WAIT_IDR 恢复次数
    std::atomic<uint64_t> frame_seq_gap{0};         // frame_seq 跳变次数
    std::atomic<uint64_t> invalid_idr{0};           // 非法 IDR 数
    std::atomic<uint64_t> idr_accepted{0};          // 接受的 IDR 数

    // M3 解码统计
    std::atomic<uint64_t> frames_decoded{0};        // 解码帧数
    std::atomic<uint64_t> decode_errors{0};         // 解码错误数
    std::atomic<uint64_t> decode_queue_size{0};     // 解码队列大小
    std::atomic<uint64_t> bytes_pushed_decode{0};   // 送入解码器的字节数

    std::chrono::steady_clock::time_point start_time;

    Metrics() : start_time(std::chrono::steady_clock::now()) {}

    /**
     * @brief 打印统计信息
     */
    void print() const;

    /**
     * @brief 生成统计字符串
     */
    std::string to_string() const;
};

} // namespace udp_video

#endif // METRICS_HPP