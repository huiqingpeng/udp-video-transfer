/**
 * @file main.cpp
 * @brief UDP 视频传输协议接收端主程序 (M4)
 *
 * 功能：
 * - 支持 1-4 路并发接收
 * - 每路独立：socket、重组、状态机、输出
 * - 输出模式：file / decode / dual
 * - 保持单路模式兼容
 */

#include "packet_header.hpp"
#include "metrics.hpp"
#include "channel_receiver.hpp"
#include "multi_channel_receiver.hpp"

#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <atomic>
#include <signal.h>
#include <vector>

namespace {

// 全局运行标志
std::atomic<bool> g_running{true};

// 全局接收器指针（用于信号处理）
std::unique_ptr<udp_video::MultiChannelReceiver> g_receiver;

// 信号处理
void signal_handler(int) {
    g_running = false;
    std::cout << "\n[MAIN] Received shutdown signal" << std::endl;
    if (g_receiver) {
        g_receiver->stop();
    }
}

} // anonymous namespace

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// main
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -c, --channel <0-3>        Single channel mode (default: 0)\n"
              << "                             Use --channels for multi-channel\n"
              << "\n"
              << "  --channels <list>          Multi-channel mode (e.g., 0,1,2,3 or 0,2)\n"
              << "                             Default: all 4 channels (0,1,2,3)\n"
              << "\n"
              << "  -o, --output <dir>         Output directory (default: ./dump)\n"
              << "\n"
              << "  --mode <file|decode|dual>  Output mode (default: file)\n"
              << "                            file:  仅落盘\n"
              << "                            decode: 仅解码显示\n"
              << "                            dual:   落盘 + 解码显示\n"
              << "\n"
              << "  --decode-channel <0-3>     Channel to decode in decode/dual mode\n"
              << "                             (default: first enabled channel)\n"
              << "\n"
              << "  --drop-once-frame-seq <N>  Fault injection: drop frag 0 of frame_seq N\n"
              << "                             (only in single-channel mode)\n"
              << "\n"
              << "  --debug-dump-au-dir <dir>  Directory to dump AU payloads for debugging\n"
              << "                             (protocol layer consistency check)\n"
              << "\n"
              << "  --debug-dump-max-au <N>    Maximum number of AU to dump (default: 100)\n"
              << "\n"
              << "  -h, --help                 Show this help\n"
              << "\n"
              << "Examples:\n"
              << "  # Single channel (channel 0)\n"
              << "  " << prog << " -c 0 -o ./dump --mode file\n"
              << "\n"
              << "  # All 4 channels with AU dump for debugging\n"
              << "  " << prog << " --channels 0 -o ./dump --mode file --debug-dump-au-dir ./au_dump_rx\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    // 默认配置
    bool single_channel_mode = true;
    uint8_t single_channel_id = 0;
    std::string channels_str = "0,1,2,3";
    std::string output_dir = "./dump";
    std::string mode_str = "file";
    int decode_channel = -1;

    // AU dump 调试参数
    std::string debug_dump_au_dir = "";
    int debug_dump_max_au = 100;

    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if ((arg == "-c" || arg == "--channel") && i + 1 < argc) {
            single_channel_mode = true;
            single_channel_id = static_cast<uint8_t>(std::stoi(argv[++i]));
            if (single_channel_id > 3) {
                std::cerr << "Invalid channel ID: " << (int)single_channel_id << std::endl;
                return 1;
            }
        } else if (arg == "--channels" && i + 1 < argc) {
            single_channel_mode = false;
            channels_str = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_dir = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            mode_str = argv[++i];
            if (mode_str != "file" && mode_str != "decode" && mode_str != "dual") {
                std::cerr << "Invalid mode: " << mode_str << std::endl;
                return 1;
            }
        } else if (arg == "--decode-channel" && i + 1 < argc) {
            decode_channel = std::stoi(argv[++i]);
            if (decode_channel < 0 || decode_channel > 3) {
                std::cerr << "Invalid decode-channel: " << decode_channel << std::endl;
                return 1;
            }
        } else if (arg == "--debug-dump-au-dir" && i + 1 < argc) {
            debug_dump_au_dir = argv[++i];
        } else if (arg == "--debug-dump-max-au" && i + 1 < argc) {
            debug_dump_max_au = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 解析输出模式
    udp_video::OutputMode mode = udp_video::OutputMode::FILE;
    if (mode_str == "decode") {
        mode = udp_video::OutputMode::DECODE;
    } else if (mode_str == "dual") {
        mode = udp_video::OutputMode::DUAL;
    }

#ifndef HAS_GSTREAMER
    // 检查解码模式（无 GStreamer 时警告）
    if (mode == udp_video::OutputMode::DECODE || mode == udp_video::OutputMode::DUAL) {
        std::cerr << "\n[WARNING] GStreamer not available. Decode mode disabled." << std::endl;
        std::cerr << "[WARNING] Falling back to file mode.\n" << std::endl;
        mode = udp_video::OutputMode::FILE;
        mode_str = "file";
    }
#endif

    // 打印启动信息
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "UDP Video Receiver M4 - Protocol v1.1 (version 0x02)" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    if (single_channel_mode) {
        std::cout << "Mode:    Single channel" << std::endl;
        std::cout << "Channel: " << (int)single_channel_id << std::endl;
        std::cout << "Port:    " << udp_video::CHANNEL_PORTS[single_channel_id] << std::endl;
    } else {
        std::cout << "Mode:    Multi-channel" << std::endl;
        std::cout << "Channels: " << channels_str << std::endl;
    }

    std::cout << "Output:  " << mode_str << std::endl;
    if (mode == udp_video::OutputMode::FILE || mode == udp_video::OutputMode::DUAL) {
        std::cout << "Dir:     " << output_dir << std::endl;
    }
    if (decode_channel >= 0 && (mode == udp_video::OutputMode::DECODE || mode == udp_video::OutputMode::DUAL)) {
        std::cout << "Decode:  Channel " << decode_channel << std::endl;
    }
    std::cout << std::endl;

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 单路模式（兼容旧命令）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (single_channel_mode) {
        udp_video::ChannelConfig config;
        config.channel_id = single_channel_id;
        config.output_dir = output_dir;
        config.mode = mode;
        config.enable_decode = (decode_channel == single_channel_id) ||
                               (decode_channel < 0 && (mode == udp_video::OutputMode::DECODE ||
                                                       mode == udp_video::OutputMode::DUAL));

        // AU dump 调试参数
        config.debug_dump_au_dir = debug_dump_au_dir;
        config.debug_dump_max_au = debug_dump_max_au;

        auto receiver = std::make_unique<udp_video::ChannelReceiver>(config);

        if (!receiver->init()) {
            return 1;
        }

        receiver->start();

        // 等待信号
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        receiver->stop();
        receiver->join();

        std::cout << "\n[MAIN] Receiver stopped" << std::endl;
        std::cout << "[MAIN] Output: " << output_dir << "/channel"
                  << (int)single_channel_id << ".h265" << std::endl;

        return 0;
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // 多路模式
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    udp_video::MultiChannelConfig config;
    config.output_dir = output_dir;
    config.mode = mode;

    // 解析 channels 字符串
    std::bitset<4> enabled_channels;
    std::stringstream ss(channels_str);
    std::string token;
    int first_enabled_channel = -1;

    while (std::getline(ss, token, ',')) {
        try {
            int ch = std::stoi(token);
            if (ch >= 0 && ch <= 3) {
                enabled_channels.set(ch);
                if (first_enabled_channel < 0) {
                    first_enabled_channel = ch;
                }
            }
        } catch (...) {
            // 忽略无效输入
        }
    }

    if (enabled_channels.none()) {
        std::cerr << "[MAIN] No valid channels specified" << std::endl;
        return 1;
    }

    config.enabled_channels = enabled_channels;

    // 设置解码通道
    if (decode_channel >= 0) {
        if (!enabled_channels.test(decode_channel)) {
            std::cerr << "[MAIN] decode-channel " << decode_channel
                      << " is not enabled" << std::endl;
            return 1;
        }
        config.decode_channel = decode_channel;
    } else if (first_enabled_channel >= 0 && (mode == udp_video::OutputMode::DECODE ||
                                               mode == udp_video::OutputMode::DUAL)) {
        config.decode_channel = first_enabled_channel;
    }

    // 创建并初始化多路接收器
    g_receiver = std::make_unique<udp_video::MultiChannelReceiver>(config);

    if (!g_receiver->init()) {
        return 1;
    }

    g_receiver->start();

    // 等待信号
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    g_receiver->stop();
    g_receiver->join();

    std::cout << "\n[MAIN] All receivers stopped" << std::endl;
    std::cout << "[MAIN] Output files: " << output_dir << "/channel*.h265" << std::endl;

    return 0;
}