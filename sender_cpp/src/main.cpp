/**
 * @file main.cpp
 * @brief UDP 视频发送端入口
 */

#include "udp_sender.hpp"

#include <iostream>
#include <string>
#include <cstring>
#include <getopt.h>

void print_usage(const char* prog_name) {
    std::cout << "Usage: " << prog_name << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  -i, --input <file>      Input video file (required)\n"
              << "  -d, --dest-ip <ip>      Destination IP address (required)\n"
              << "  -c, --channel <0-3>     Channel ID (default: 0)\n"
              << "  -f, --fps <num>         Frame rate (default: 25)\n"
              << "  -g, --gop <num>         GOP size (default: 25)\n"
              << "  -b, --bitrate <value>   Bitrate, e.g., 4M, 2000K (default: 4M)\n"
              << "  -p, --pacing-us <us>    Pacing between fragments in microseconds (default: 0)\n"
              << "  -v, --verbose           Verbose output\n"
              << "  -h, --help              Show this help message\n"
              << "\n"
              << "Debug options:\n"
              << "  --debug-dump-dir <dir>  Directory to dump AU payloads\n"
              << "  --debug-dump-max <num>  Maximum number of AU to dump (default: 100)\n"
              << std::endl;
}

int main(int argc, char* argv[]) {
    udp_video::SenderConfig config;

    static struct option long_options[] = {
        {"input",           required_argument, nullptr, 'i'},
        {"dest-ip",         required_argument, nullptr, 'd'},
        {"channel",         required_argument, nullptr, 'c'},
        {"fps",             required_argument, nullptr, 'f'},
        {"gop",             required_argument, nullptr, 'g'},
        {"bitrate",         required_argument, nullptr, 'b'},
        {"pacing-us",       required_argument, nullptr, 'p'},
        {"verbose",         no_argument,       nullptr, 'v'},
        {"help",            no_argument,       nullptr, 'h'},
        {"debug-dump-dir",  required_argument, nullptr, 'D'},
        {"debug-dump-max",  required_argument, nullptr, 'M'},
        {nullptr, 0, nullptr, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "i:d:c:f:g:b:p:vh", long_options, nullptr)) != -1) {
        switch (opt) {
            case 'i':
                config.input_file = optarg;
                break;
            case 'd':
                config.dest_ip = optarg;
                break;
            case 'c':
                config.channel = static_cast<uint8_t>(std::stoi(optarg));
                if (config.channel > 3) {
                    std::cerr << "Error: channel must be 0-3" << std::endl;
                    return 1;
                }
                break;
            case 'f':
                config.fps = std::stoi(optarg);
                break;
            case 'g':
                config.gop = std::stoi(optarg);
                break;
            case 'b':
                config.bitrate = optarg;
                break;
            case 'p':
                config.pacing_us = std::stoi(optarg);
                break;
            case 'v':
                config.verbose = true;
                break;
            case 'D':
                config.debug_dump_dir = optarg;
                break;
            case 'M':
                config.debug_dump_max = std::stoi(optarg);
                break;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // 检查必需参数
    if (config.input_file.empty()) {
        std::cerr << "Error: --input is required" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    if (config.dest_ip.empty()) {
        std::cerr << "Error: --dest-ip is required" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    // 打印配置
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "UDP Video Sender - Protocol v1.1 (version 0x02)" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "Input:    " << config.input_file << std::endl;
    std::cout << "Dest IP:  " << config.dest_ip << std::endl;
    std::cout << "Channel:  " << static_cast<int>(config.channel) << std::endl;
    std::cout << "FPS:      " << config.fps << std::endl;
    std::cout << "GOP:      " << config.gop << std::endl;
    std::cout << "Bitrate:  " << config.bitrate << std::endl;
    if (config.pacing_us > 0) {
        std::cout << "Pacing:   " << config.pacing_us << " us" << std::endl;
    }
    std::cout << std::endl;

    try {
        udp_video::UdpSender sender(config);
        sender.run();
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        return 1;
    }

    return 0;
}