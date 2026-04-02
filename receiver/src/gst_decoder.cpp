/**
 * @file gst_decoder.cpp
 * @brief GStreamer H.265 硬件解码器实现
 */

#include "gst_decoder.hpp"
#include <iostream>
#include <cstring>

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <gst/app/gstappsink.h>

namespace udp_video {

GstDecoder::GstDecoder() : config_() {}

GstDecoder::GstDecoder(const GstDecoderConfig& config) : config_(config) {}

GstDecoder::~GstDecoder() {
    stop();
}

bool GstDecoder::init() {
    // 初始化 GStreamer（只初始化一次）
    static bool gst_initialized = false;
    if (!gst_initialized) {
        gst_init(nullptr, nullptr);
        gst_initialized = true;
    }

    // 检测是否有显示环境
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    bool has_display = (display != nullptr && display[0] != '\0') ||
                        (wayland != nullptr && wayland[0] != '\0');

    if (!has_display) {
        std::cout << "[GST] No display available, using fakesink" << std::endl;
        config_.enable_display = false;
    }

    // 构建 pipeline 字符串
    // 注意：不指定 width/height/framerate，让 h265parse 从码流中解析
    std::string pipeline_str;
    if (config_.enable_display) {
        // 使用 nvv4l2decoder 硬件解码 + nveglglessink 显示
        pipeline_str = "appsrc name=src "
                       "caps=video/x-h265,stream-format=byte-stream,alignment=au "
                       "! h265parse "
                       "! nvv4l2decoder "
                       "! nveglglessink";
    } else {
        // 仅解码，不显示（用于无显示环境或测试）
        pipeline_str = "appsrc name=src "
                       "caps=video/x-h265,stream-format=byte-stream,alignment=au "
                       "! h265parse "
                       "! nvv4l2decoder "
                       "! fakesink";
    }

    std::cout << "[GST] Pipeline: " << pipeline_str << std::endl;

    // 解析 pipeline
    GError* error = nullptr;
    pipeline_ = gst_parse_launch(pipeline_str.c_str(), &error);
    if (error) {
        std::cerr << "[GST] Pipeline parse error: " << error->message << std::endl;
        g_error_free(error);
        return false;
    }

    if (!pipeline_) {
        std::cerr << "[GST] Failed to create pipeline" << std::endl;
        return false;
    }

    // 获取 appsrc 元素
    appsrc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "src");
    if (!appsrc_) {
        std::cerr << "[GST] Failed to get appsrc element" << std::endl;
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    // 配置 appsrc
    g_object_set(appsrc_,
                 "stream-type", GST_APP_STREAM_TYPE_STREAM,
                 "format", GST_FORMAT_TIME,
                 "is-live", TRUE,
                 "do-timestamp", TRUE,  // 自动添加时间戳
                 nullptr);

    // 注册 appsrc 信号回调
    g_signal_connect(appsrc_, "need-data", G_CALLBACK(on_need_data), this);
    g_signal_connect(appsrc_, "enough-data", G_CALLBACK(on_enough_data), this);

    // 获取 bus 并注册消息回调
    GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
    gst_bus_add_watch(bus, on_bus_message, this);
    gst_object_unref(bus);

    // 启动 GStreamer 主循环线程
    running_ = true;
    gst_thread_ = std::thread(&GstDecoder::run_gst_loop, this);

    // 等待 pipeline 启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    initialized_ = true;
    std::cout << "[GST] Decoder initialized successfully" << std::endl;
    return true;
}

void GstDecoder::run_gst_loop() {
    // 创建主循环
    main_loop_ = g_main_loop_new(nullptr, FALSE);

    // 启动 pipeline
    GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::cerr << "[GST] Failed to set pipeline to PLAYING" << std::endl;
        increment_decode_error();
        g_main_loop_quit(main_loop_);
        return;
    }

    std::cout << "[GST] Pipeline started (PLAYING)" << std::endl;

    // 运行主循环
    g_main_loop_run(main_loop_);

    // 清理
    gst_element_set_state(pipeline_, GST_STATE_NULL);
    g_main_loop_unref(main_loop_);
    main_loop_ = nullptr;

    std::cout << "[GST] Pipeline stopped" << std::endl;
}

bool GstDecoder::push_annexb_data(const std::vector<uint8_t>& annexb_data, uint64_t pts) {
    if (!initialized_ || !running_) {
        return false;
    }

    // 直接推送数据到 appsrc（同步方式，不用 need-data 信号）
    if (!appsrc_) {
        return false;
    }

    // 创建 GstBuffer
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, annexb_data.size(), nullptr);
    if (!buffer) {
        std::cerr << "[GST] Failed to allocate buffer" << std::endl;
        increment_decode_error();
        return false;
    }

    // 填充数据
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        std::cerr << "[GST] Failed to map buffer" << std::endl;
        gst_buffer_unref(buffer);
        increment_decode_error();
        return false;
    }

    memcpy(map.data, annexb_data.data(), annexb_data.size());
    gst_buffer_unmap(buffer, &map);

    // 设置 PTS（90kHz 转 nanosecond）
    if (pts > 0) {
        GST_BUFFER_PTS(buffer) = pts * 11111;  // 90kHz -> ns (1/90000 * 1e9)
    }

    // 推送到 appsrc
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
    if (ret == GST_FLOW_OK) {
        bytes_pushed_ += annexb_data.size();
        frames_decoded_++;
        return true;
    } else if (ret == GST_FLOW_FLUSHING) {
        // Pipeline 正在关闭，不是错误
        return false;
    } else {
        // 其他错误
        increment_decode_error();
        return false;
    }
}

void GstDecoder::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    initialized_ = false;

    // 通知队列消费者退出
    queue_cv_.notify_all();

    // 停止 GStreamer 主循环
    if (main_loop_) {
        g_main_loop_quit(main_loop_);
    }

    // 等待线程结束
    if (gst_thread_.joinable()) {
        gst_thread_.join();
    }

    // 清理 GStreamer 对象
    if (appsrc_) {
        gst_object_unref(appsrc_);
        appsrc_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }

    // 清空队列
    std::lock_guard<std::mutex> lock(queue_mutex_);
    while (!au_queue_.empty()) {
        au_queue_.pop();
    }

    std::cout << "[GST] Decoder stopped. Stats: frames=" << frames_decoded_.load()
              << " errors=" << decode_errors_.load()
              << " bytes=" << bytes_pushed_.load() << std::endl;
}

size_t GstDecoder::get_queue_size() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(queue_mutex_));
    return au_queue_.size();
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// GStreamer 回调函数
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void GstDecoder::on_need_data(GstElement* appsrc, guint size, gpointer user_data) {
    GstDecoder* decoder = static_cast<GstDecoder*>(user_data);

    if (!decoder->running_) {
        return;
    }

    // 从队列获取数据
    AuData au;
    {
        std::unique_lock<std::mutex> lock(decoder->queue_mutex_);
        // 等待数据或超时（10ms）
        decoder->queue_cv_.wait_for(lock, std::chrono::milliseconds(10), [&decoder] {
            return !decoder->au_queue_.empty() || !decoder->running_;
        });

        if (!decoder->running_ || decoder->au_queue_.empty()) {
            return;
        }

        au = decoder->au_queue_.front();
        decoder->au_queue_.pop();
    }

    // 推送 buffer 到 appsrc
    if (decoder->push_buffer_to_appsrc(au)) {
        decoder->frames_decoded_++;
    } else {
        decoder->decode_errors_++;
    }
}

void GstDecoder::on_enough_data(GstElement* appsrc, gpointer user_data) {
    // appsrc 缓冲区足够，暂时不需要更多数据
    // 不做特殊处理，need-data 信号会在需要时再次触发
}

GstFlowReturn GstDecoder::on_new_sample(GstElement* sink, gpointer user_data) {
    // 从 appsink 获取解码后的帧（用于统计）
    GstDecoder* decoder = static_cast<GstDecoder*>(user_data);

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (sample) {
        decoder->frames_decoded_++;
        gst_sample_unref(sample);
        return GST_FLOW_OK;
    }

    decoder->decode_errors_++;
    return GST_FLOW_ERROR;
}

gboolean GstDecoder::on_bus_message(GstBus* bus, GstMessage* msg, gpointer user_data) {
    GstDecoder* decoder = static_cast<GstDecoder*>(user_data);

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &error, &debug);

            std::cerr << "[GST] ERROR: " << error->message << std::endl;
            if (debug) {
                std::cerr << "[GST] Debug: " << debug << std::endl;
            }

            decoder->increment_decode_error();

            g_error_free(error);
            g_free(debug);

            // 严重错误，停止 pipeline
            if (decoder->main_loop_) {
                g_main_loop_quit(decoder->main_loop_);
            }
            break;
        }

        case GST_MESSAGE_WARNING: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_warning(msg, &error, &debug);

            std::cerr << "[GST] WARNING: " << error->message << std::endl;

            g_error_free(error);
            g_free(debug);
            break;
        }

        case GST_MESSAGE_EOS: {
            std::cout << "[GST] End of stream" << std::endl;
            if (decoder->main_loop_) {
                g_main_loop_quit(decoder->main_loop_);
            }
            break;
        }

        case GST_MESSAGE_STATE_CHANGED: {
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);

            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(decoder->pipeline_)) {
                std::cout << "[GST] State changed: "
                          << gst_element_state_get_name(old_state) << " -> "
                          << gst_element_state_get_name(new_state) << std::endl;
            }
            break;
        }

        default:
            break;
    }

    return TRUE;
}

bool GstDecoder::push_buffer_to_appsrc(const AuData& au) {
    if (!appsrc_ || !running_) {
        return false;
    }

    // 创建 GstBuffer
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, au.data.size(), nullptr);
    if (!buffer) {
        std::cerr << "[GST] Failed to allocate buffer" << std::endl;
        return false;
    }

    // 填充数据
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        std::cerr << "[GST] Failed to map buffer" << std::endl;
        gst_buffer_unref(buffer);
        return false;
    }

    memcpy(map.data, au.data.data(), au.data.size());
    gst_buffer_unmap(buffer, &map);

    // 设置 PTS（90kHz 转 nanosecond）
    if (au.pts > 0) {
        GST_BUFFER_PTS(buffer) = au.pts * 11111;  // 90kHz -> ns (1/90000 * 1e9)
    }

    // 推送到 appsrc
    GstFlowReturn ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
    if (ret != GST_FLOW_OK) {
        std::cerr << "[GST] push_buffer failed: " << ret << std::endl;
        return false;
    }

    return true;
}

void GstDecoder::increment_decode_error() {
    decode_errors_++;
}

} // namespace udp_video