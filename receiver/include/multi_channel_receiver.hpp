/**
 * @file multi_channel_receiver.hpp
 * @brief 多路并发接收器
 *
 * 管理 1-4 个独立通道，每个通道：
 * - 独立 socket 和接收线程
 * - 独立重组上下文和状态机
 * - 独立输出文件和解码器（可选）
 * - 独立统计
 */

#ifndef MULTI_CHANNEL_RECEIVER_HPP
#define MULTI_CHANNEL_RECEIVER_HPP

#include "channel_receiver.hpp"
#include "metrics.hpp"

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <bitset>

namespace udp_video {

/**
 * @brief 多路接收器配置
 */
struct MultiChannelConfig {
    std::bitset<4> enabled_channels{0xF};  // 默认启用所有 4 路
    std::string output_dir{"./dump"};
    OutputMode mode{OutputMode::FILE};
    int decode_channel{-1};  // 解码通道，-1 表示不解码
};

/**
 * @brief 多路并发接收器
 */
class MultiChannelReceiver {
public:
    MultiChannelReceiver();
    explicit MultiChannelReceiver(const MultiChannelConfig& config);
    ~MultiChannelReceiver();

    /**
     * @brief 初始化所有启用的通道
     * @return 成功返回 true
     */
    bool init();

    /**
     * @brief 启动所有通道
     */
    void start();

    /**
     * @brief 停止所有通道
     */
    void stop();

    /**
     * @brief 等待所有通道结束
     */
    void join();

    /**
     * @brief 获取通道接收器
     */
    ChannelReceiver* get_channel(uint8_t channel_id);

    /**
     * @brief 获取总览统计
     */
    std::string get_aggregate_stats() const;

    /**
     * @brief 打印总览统计
     */
    void print_aggregate_stats();

private:
    MultiChannelConfig config_;
    std::vector<std::unique_ptr<ChannelReceiver>> channels_;
    Metrics aggregate_metrics_;

    // 统计打印线程
    std::thread stats_thread_;
    std::atomic<bool> running_{false};
    std::chrono::steady_clock::time_point start_time_;

    void stats_loop();
};

} // namespace udp_video

#endif // MULTI_CHANNEL_RECEIVER_HPP