/**
 * @file gst_decoder.hpp
 * @brief GStreamer H.265 硬件解码器（Jetson Orin Nano）
 *
 * 功能：
 * - 使用 NVIDIA nvv4l2decoder 硬件解码
 * - appsrc 输入 Annex B H.265 字节流
 * - nveglglessink 显示输出
 * - 独立线程运行 GStreamer pipeline
 */

#ifndef GST_DECODER_HPP
#define GST_DECODER_HPP

#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <queue>
#include <condition_variable>

// GLib 和 GStreamer 头文件
#include <glib.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

namespace udp_video {

/**
 * @brief GStreamer 解码器配置
 */
struct GstDecoderConfig {
    int width{640};
    int height{480};
    int fps{25};
    bool enable_display{true};  // 是否启用显示（无显示时使用 fakesink）
    int queue_max_size{10};  // AU 队列最大长度
};

/**
 * @brief GStreamer H.265 解码器
 *
 * 使用 appsrc + h265parse + nvv4l2decoder + nveglglessink pipeline
 */
class GstDecoder {
public:
    GstDecoder();
    explicit GstDecoder(const GstDecoderConfig& config);
    ~GstDecoder();

    /**
     * @brief 初始化 GStreamer pipeline
     * @return 成功返回 true
     */
    bool init();

    /**
     * @brief 推送 Annex B 数据到解码队列
     * @param annexb_data Annex B 格式的 H.265 数据（含起始码）
     * @param pts PTS 时间戳（90kHz），可选
     * @return 成功推送返回 true，队列满返回 false
     */
    bool push_annexb_data(const std::vector<uint8_t>& annexb_data, uint64_t pts = 0);

    /**
     * @brief 停止解码器
     */
    void stop();

    /**
     * @brief 获取已解码帧数
     */
    uint64_t get_frames_decoded() const { return frames_decoded_.load(); }

    /**
     * @brief 获取解码错误数
     */
    uint64_t get_decode_errors() const { return decode_errors_.load(); }

    /**
     * @brief 获取当前队列大小
     */
    size_t get_queue_size() const;

    /**
     * @brief 检查是否正在运行
     */
    bool is_running() const { return running_.load(); }

private:
    // GStreamer elements
    GstElement* pipeline_{nullptr};
    GstElement* appsrc_{nullptr};
    GstElement* sink_{nullptr};
    GMainLoop* main_loop_{nullptr};

    // 线程
    std::thread gst_thread_;

    // 状态
    std::atomic<bool> running_{false};
    std::atomic<bool> initialized_{false};
    GstDecoderConfig config_;

    // 统计
    std::atomic<uint64_t> frames_decoded_{0};
    std::atomic<uint64_t> decode_errors_{0};
    std::atomic<uint64_t> bytes_pushed_{0};

    // AU 队列（生产者-消费者）
    struct AuData {
        std::vector<uint8_t> data;
        uint64_t pts;
    };
    std::queue<AuData> au_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;

    // GStreamer 回调
    static void on_need_data(GstElement* appsrc, guint size, gpointer user_data);
    static void on_enough_data(GstElement* appsrc, gpointer user_data);
    static GstFlowReturn on_new_sample(GstElement* sink, gpointer user_data);
    static gboolean on_bus_message(GstBus* bus, GstMessage* msg, gpointer user_data);

    // 内部方法
    void run_gst_loop();
    bool push_buffer_to_appsrc(const AuData& au);
    void increment_decode_error();
};

} // namespace udp_video

#endif // GST_DECODER_HPP