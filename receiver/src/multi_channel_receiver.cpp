/**
 * @file multi_channel_receiver.cpp
 * @brief 多路并发接收器实现
 */

#include "multi_channel_receiver.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace udp_video {

MultiChannelReceiver::MultiChannelReceiver()
    : start_time_(std::chrono::steady_clock::now()) {
}

MultiChannelReceiver::MultiChannelReceiver(const MultiChannelConfig& config)
    : config_(config)
    , start_time_(std::chrono::steady_clock::now()) {
}

MultiChannelReceiver::~MultiChannelReceiver() {
    stop();
}

bool MultiChannelReceiver::init() {
    // 创建每个启用的通道
    for (int ch = 0; ch < 4; ++ch) {
        if (!config_.enabled_channels.test(ch)) {
            continue;
        }

        ChannelConfig ch_config;
        ch_config.channel_id = static_cast<uint8_t>(ch);
        ch_config.output_dir = config_.output_dir;
        ch_config.mode = config_.mode;
        ch_config.enable_decode = (config_.decode_channel == ch);

        auto receiver = std::make_unique<ChannelReceiver>(ch_config);
        if (!receiver->init()) {
            std::cerr << "[MAIN] Failed to init channel " << ch << std::endl;
            // 继续尝试其他通道
            continue;
        }

        channels_.push_back(std::move(receiver));
    }

    if (channels_.empty()) {
        std::cerr << "[MAIN] No channels initialized" << std::endl;
        return false;
    }

    std::cout << "[MAIN] Initialized " << channels_.size() << " channel(s)" << std::endl;
    return true;
}

void MultiChannelReceiver::start() {
    running_ = true;

    // 启动所有通道
    for (auto& ch : channels_) {
        ch->start();
    }

    // 启动统计线程
    stats_thread_ = std::thread(&MultiChannelReceiver::stats_loop, this);
}

void MultiChannelReceiver::stop() {
    running_ = false;

    // 停止所有通道
    for (auto& ch : channels_) {
        ch->stop();
    }

    // 等待统计线程
    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }

    // 等待所有通道结束
    for (auto& ch : channels_) {
        ch->join();
    }
}

void MultiChannelReceiver::join() {
    for (auto& ch : channels_) {
        ch->join();
    }

    if (stats_thread_.joinable()) {
        stats_thread_.join();
    }
}

ChannelReceiver* MultiChannelReceiver::get_channel(uint8_t channel_id) {
    for (auto& ch : channels_) {
        if (ch->get_channel_id() == channel_id) {
            return ch.get();
        }
    }
    return nullptr;
}

void MultiChannelReceiver::stats_loop() {
    auto last_print = std::chrono::steady_clock::now();

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_print);

        if (elapsed.count() >= 5) {
            print_aggregate_stats();
            last_print = now;
        }
    }
}

void MultiChannelReceiver::print_aggregate_stats() {
    // 汇总所有通道统计
    uint64_t total_packets = 0;
    uint64_t total_valid = 0;
    uint64_t total_crc_fail = 0;
    uint64_t total_au_completed = 0;
    uint64_t total_au_timeout = 0;
    uint64_t total_bytes_recv = 0;
    uint64_t total_bytes_written = 0;
    uint64_t total_frames_decoded = 0;

    for (const auto& ch : channels_) {
        const auto& m = ch->get_metrics();
        total_packets += m.total_packets.load();
        total_valid += m.valid_packets.load();
        total_crc_fail += m.crc_fail.load();
        total_au_completed += m.au_completed.load();
        total_au_timeout += m.au_timeout.load();
        total_bytes_recv += m.bytes_received.load();
        total_bytes_written += m.bytes_written.load();
        total_frames_decoded += m.frames_decoded.load();
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time_);

    std::cout << "\n━━━ [AGGREGATE] ━━━" << std::endl;
    std::cout << "Channels: " << channels_.size()
              << " | Elapsed: " << elapsed.count() << "s" << std::endl;
    std::cout << "Packets: " << total_packets
              << " | Valid: " << total_valid
              << " | CRC_fail: " << total_crc_fail << std::endl;
    std::cout << "AU_completed: " << total_au_completed
              << " | AU_timeout: " << total_au_timeout << std::endl;
    std::cout << "Bytes_recv: " << total_bytes_recv
              << " | Bytes_written: " << total_bytes_written << std::endl;

    if (total_frames_decoded > 0) {
        std::cout << "Frames_decoded: " << total_frames_decoded << std::endl;
    }

    std::cout << "━━━━━━━━━━━━━━━━━━━" << std::endl;
}

std::string MultiChannelReceiver::get_aggregate_stats() const {
    std::ostringstream oss;

    uint64_t total_packets = 0;
    uint64_t total_au_completed = 0;

    for (const auto& ch : channels_) {
        const auto& m = ch->get_metrics();
        total_packets += m.total_packets.load();
        total_au_completed += m.au_completed.load();
    }

    oss << "channels=" << channels_.size()
        << " packets=" << total_packets
        << " au=" << total_au_completed;

    return oss.str();
}

} // namespace udp_video