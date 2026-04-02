/**
 * @file channel_receiver.hpp
 * @brief 单路接收器（封装单路所有上下文）
 *
 * 包含：
 * - socket 管理
 * - AU 重组
 * - IDR 状态机
 * - 输出（文件/解码）
 * - 统计
 */

#ifndef CHANNEL_RECEIVER_HPP
#define CHANNEL_RECEIVER_HPP

#include "packet_header.hpp"
#include "reassembly.hpp"
#include "annexb_writer.hpp"
#include "idr_fsm.hpp"
#include "metrics.hpp"

#ifdef HAS_GSTREAMER
#include "gst_decoder.hpp"
#endif

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <memory>

namespace udp_video {

/**
 * @brief 单路接收器配置
 */
struct ChannelConfig {
    uint8_t channel_id{0};
    std::string output_dir{"./dump"};
    OutputMode mode{OutputMode::FILE};
    bool enable_decode{false};

    // AU dump 调试参数
    std::string debug_dump_au_dir{};  // dump 目录
    int debug_dump_max_au{100};        // 最大 dump 数量
};

/**
 * @brief 单路接收器
 *
 * 封装单路接收的所有上下文，支持独立线程运行
 */
class ChannelReceiver {
public:
    ChannelReceiver(const ChannelConfig& config);
    ~ChannelReceiver();

    /**
     * @brief 初始化（创建 socket、打开文件、初始化解码器）
     * @return 成功返回 true
     */
    bool init();

    /**
     * @brief 启动接收线程
     */
    void start();

    /**
     * @brief 停止接收
     */
    void stop();

    /**
     * @brief 等待线程结束
     */
    void join();

    /**
     * @brief 检查是否正在运行
     */
    bool is_running() const { return running_.load(); }

    /**
     * @brief 获取统计信息
     */
    const Metrics& get_metrics() const { return metrics_; }

    /**
     * @brief 获取通道 ID
     */
    uint8_t get_channel_id() const { return config_.channel_id; }

private:
    /**
     * @brief 接收线程主函数
     */
    void receive_loop();

    /**
     * @brief 处理完整的 AU
     */
    void process_completed_au(const std::vector<uint8_t>& au_data, const PacketHeader& header);

    /**
     * @brief 处理输出
     */
    bool process_output(const std::vector<uint8_t>& au_data, const PacketHeader& header);

    /**
     * @brief 处理解析错误
     */
    void handle_parse_error(ParseResult result, const PacketHeader& header);

    /**
     * @brief 打印统计
     */
    void print_stats();

    /**
     * @brief AU dump（在重组完成后、Annex B 恢复前）
     */
    void dump_au(const std::vector<uint8_t>& au_data, const PacketHeader& header);

    /**
     * @brief 写入 AU dump manifest
     */
    void write_dump_manifest();

    ChannelConfig config_;
    int socket_{-1};

    // 组件
    std::unique_ptr<ReassemblyManager> reassembly_;
    std::unique_ptr<AnnexBWriter> writer_;
#ifdef HAS_GSTREAMER
    std::unique_ptr<GstDecoder> decoder_;
#endif
    std::unique_ptr<IdrFsm> fsm_;
    Metrics metrics_;

    // 线程
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};

    // AU dump 调试
    int debug_dump_count_{0};
    std::vector<std::string> debug_dump_manifest_;  // JSON 格式的元信息行
};

} // namespace udp_video

#endif // CHANNEL_RECEIVER_HPP