/**
 * @file udp_sender.cpp
 * @brief UDP 视频发送端主类实现
 */

#include "udp_sender.hpp"
#include "crc16.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstring>
#include <algorithm>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace udp_video {

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SenderStats 实现
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void SenderStats::print_stats() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

    std::cout << "[Stats] AU=" << au_sent
              << " IDR=" << idr_sent
              << " Packets=" << packets_sent
              << " Bytes=" << bytes_sent
              << " Errors=" << send_errors
              << " Seq=" << current_frame_seq
              << " Elapsed=" << elapsed << "s"
              << std::endl;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// UdpSender 实现
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

UdpSender::UdpSender(const SenderConfig& config)
    : config_(config) {
}

UdpSender::~UdpSender() {
    stop();
}

void UdpSender::stop() {
    running_ = false;
}

int UdpSender::parse_bitrate() const {
    std::string bitrate = config_.bitrate;
    int multiplier = 1;

    if (bitrate.back() == 'M' || bitrate.back() == 'm') {
        multiplier = 1000000;
        bitrate.pop_back();
    } else if (bitrate.back() == 'K' || bitrate.back() == 'k') {
        multiplier = 1000;
        bitrate.pop_back();
    }

    return std::stoi(bitrate) * multiplier;
}

std::string UdpSender::build_ffmpeg_cmd() const {
    std::ostringstream cmd;

    int bitrate_val = parse_bitrate();

    cmd << "ffmpeg"
        << " -i " << config_.input_file
        << " -c:v libx265"
        << " -x265-params 'keyint=" << config_.gop
        << ":min-keyint=" << config_.gop
        << ":no-scenecut=1:bframes=0:aud=1:repeat-headers=1:log-level=none:info=0'"
        << " -b:v " << bitrate_val
        << " -r " << config_.fps
        << " -f hevc"
        << " -loglevel quiet"
        << " pipe:1";

    return cmd.str();
}

void UdpSender::setup() {
    // 创建 UDP socket
    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        throw std::runtime_error("Failed to create socket: " + std::string(strerror(errno)));
    }

    // 设置发送缓冲区
    int sndbuf = 8 * 1024 * 1024;
    setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));

    // 启动 FFmpeg
    std::string cmd = build_ffmpeg_cmd();
    if (config_.verbose) {
        std::cout << "[INFO] FFmpeg cmd: " << cmd << std::endl;
    }

    ffmpeg_pipe_ = popen(cmd.c_str(), "r");
    if (!ffmpeg_pipe_) {
        close(sock_);
        throw std::runtime_error("Failed to start FFmpeg");
    }

    std::cout << "[INFO] FFmpeg started" << std::endl;

    uint16_t port = CHANNEL_PORTS[config_.channel];
    std::cout << "[INFO] UDP socket ready: " << config_.dest_ip << ":" << port << std::endl;
}

std::optional<AccessUnit> UdpSender::process_nal(NalUnit& nal, uint64_t pts) {
    // 缓存参数集
    if (nal.nal_type == static_cast<uint8_t>(H265NalType::VPS)) {
        cached_vps_ = nal.data;
        if (config_.verbose) {
            std::cout << "[DEBUG] Cached VPS (" << nal.data.size() << " bytes)" << std::endl;
        }
    } else if (nal.nal_type == static_cast<uint8_t>(H265NalType::SPS)) {
        cached_sps_ = nal.data;
        if (config_.verbose) {
            std::cout << "[DEBUG] Cached SPS (" << nal.data.size() << " bytes)" << std::endl;
        }
    } else if (nal.nal_type == static_cast<uint8_t>(H265NalType::PPS)) {
        cached_pps_ = nal.data;
        if (config_.verbose) {
            std::cout << "[DEBUG] Cached PPS (" << nal.data.size() << " bytes)" << std::endl;
        }
    }

    // AUD 作为 AU 边界
    if (nal.nal_type == static_cast<uint8_t>(H265NalType::AUD)) {
        // 收到 AUD，结束当前 AU
        std::optional<AccessUnit> au_to_return;

        if (!pending_nals_.empty()) {
            au_to_return = build_au_from_nals(
                pending_nals_,
                pending_pts_,
                cached_vps_.empty() ? nullptr : &cached_vps_,
                cached_sps_.empty() ? nullptr : &cached_sps_,
                cached_pps_.empty() ? nullptr : &cached_pps_
            );
            pending_nals_.clear();
        }

        // 将 AUD 添加到新的 pending_nals
        pending_nals_.push_back(nal);
        pending_pts_ = pts;
        stats_.nals_parsed++;

        return au_to_return;
    }

    // 添加到待处理列表
    pending_nals_.push_back(nal);
    pending_pts_ = pts;
    stats_.nals_parsed++;

    return std::nullopt;
}

void UdpSender::send_au(const AccessUnit& au) {
    // 序列化 AU
    std::vector<uint8_t> au_payload = serialize_au(au);
    size_t au_size = au_payload.size();

    if (au_size == 0) {
        std::cerr << "[WARN] Empty AU, skip" << std::endl;
        stats_.au_parse_errors++;
        return;
    }

    // AU dump（调试）
    if (!config_.debug_dump_dir.empty()) {
        dump_au(au_payload, au, frame_seq_);
    }

    // 构建协议头模板
    ProtocolHeader header_template;
    header_template.channel_id = config_.channel;
    header_template.frame_seq = frame_seq_;
    header_template.is_idr = au.is_idr ? 1 : 0;
    header_template.primary_nal_type = au.primary_nal_type;
    header_template.au_nal_count = static_cast<uint16_t>(au.nals.size());
    header_template.pts = au.pts;

    // 分片
    std::vector<std::vector<uint8_t>> fragments = build_fragments(au_payload, header_template);

    // 准备目标地址
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(CHANNEL_PORTS[config_.channel]);
    inet_pton(AF_INET, config_.dest_ip.c_str(), &dest_addr.sin_addr);

    // 发送
    for (const auto& frag : fragments) {
        ssize_t sent = sendto(sock_, frag.data(), frag.size(), 0,
                              (struct sockaddr*)&dest_addr, sizeof(dest_addr));

        if (sent < 0) {
            std::cerr << "[ERROR] Send error: " << strerror(errno) << std::endl;
            stats_.send_errors++;
        } else {
            stats_.packets_sent++;
            stats_.bytes_sent += frag.size();
        }

        // Pacing
        if (config_.pacing_us > 0) {
            usleep(config_.pacing_us);
        }
    }

    // 更新统计
    stats_.au_sent++;
    if (au.is_idr) {
        stats_.idr_sent++;
        std::cout << "[INFO] Sent IDR AU seq=" << frame_seq_
                  << " size=" << au_size
                  << " frags=" << fragments.size() << std::endl;
    } else if (config_.verbose) {
        std::cout << "[DEBUG] Sent AU seq=" << frame_seq_
                  << " size=" << au_size
                  << " frags=" << fragments.size() << std::endl;
    }

    stats_.current_frame_seq = frame_seq_;
    frame_seq_++;
}

void UdpSender::run() {
    running_ = true;
    setup();

    // 读取缓冲
    std::vector<uint8_t> buffer;
    buffer.reserve(1024 * 1024);  // 1MB 预留
    constexpr size_t chunk_size = 4096;

    try {
        while (running_) {
            // 从 FFmpeg 读取数据
            std::vector<uint8_t> chunk(chunk_size);
            size_t n = fread(chunk.data(), 1, chunk_size, ffmpeg_pipe_);

            if (n == 0) {
                // FFmpeg 结束，处理 buffer 中剩余的 NAL
                std::cout << "[INFO] FFmpeg finished" << std::endl;

                // 处理 buffer 中剩余的完整 NAL
                if (!buffer.empty()) {
                    size_t parsed_len = 0;
                    std::vector<NalUnit> nals = parse_nal_units(buffer, parsed_len);

                    if (!nals.empty()) {
                        for (auto& nal : nals) {
                            uint64_t pts = static_cast<uint64_t>(frame_seq_) * (90000 / config_.fps);
                            auto au = process_nal(nal, pts);
                            if (au) {
                                send_au(*au);
                            }
                        }
                        buffer.erase(buffer.begin(), buffer.begin() + parsed_len);
                    }

                    // 处理最后一个不完整的 NAL
                    if (!buffer.empty()) {
                        size_t nal_start = 0;
                        uint8_t sc_len = 0;
                        if (find_start_code(buffer, 0, nal_start, sc_len)) {
                            std::vector<uint8_t> last_nal_data(buffer.begin() + nal_start, buffer.end());
                            if (!last_nal_data.empty()) {
                                uint8_t nal_type = (last_nal_data[0] >> 1) & 0x3F;
                                NalUnit last_nal(nal_type, last_nal_data, sc_len);
                                uint64_t pts = static_cast<uint64_t>(frame_seq_) * (90000 / config_.fps);
                                auto au = process_nal(last_nal, pts);
                                if (au) {
                                    send_au(*au);
                                }
                            }
                        }
                    }
                }
                break;
            }

            // 添加到 buffer
            buffer.insert(buffer.end(), chunk.begin(), chunk.begin() + n);

            // 解析 NAL
            size_t parsed_len = 0;
            std::vector<NalUnit> nals = parse_nal_units(buffer, parsed_len);

            if (!nals.empty()) {
                // 处理每个 NAL
                for (auto& nal : nals) {
                    uint64_t pts = static_cast<uint64_t>(frame_seq_) * (90000 / config_.fps);
                    auto au = process_nal(nal, pts);
                    if (au) {
                        send_au(*au);
                    }
                }

                // 清空已解析部分
                buffer.erase(buffer.begin(), buffer.begin() + parsed_len);
            }

            // 定期打印统计
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - stats_.last_print_time).count();
            if (elapsed >= 5) {
                stats_.print_stats();
                stats_.last_print_time = now;
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[ERROR] Main loop error: " << e.what() << std::endl;
    }

    // 处理剩余 NAL
    if (!pending_nals_.empty()) {
        uint64_t pts = static_cast<uint64_t>(frame_seq_) * (90000 / config_.fps);
        auto au = build_au_from_nals(
            pending_nals_,
            pts,
            cached_vps_.empty() ? nullptr : &cached_vps_,
            cached_sps_.empty() ? nullptr : &cached_sps_,
            cached_pps_.empty() ? nullptr : &cached_pps_
        );
        if (au) {
            send_au(*au);
        }
    }

    // 写入 manifest
    write_manifest();

    // 最终统计
    stats_.print_stats();

    // 清理
    if (ffmpeg_pipe_) {
        pclose(ffmpeg_pipe_);
        ffmpeg_pipe_ = nullptr;
    }

    if (sock_ >= 0) {
        close(sock_);
        sock_ = -1;
    }

    std::cout << "[INFO] Sender stopped" << std::endl;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// AU dump 调试功能
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void UdpSender::dump_au(const std::vector<uint8_t>& au_payload, const AccessUnit& au, uint32_t frame_seq) {
    if (config_.debug_dump_dir.empty() || debug_dump_count_ >= config_.debug_dump_max) {
        return;
    }

    // 文件名
    std::ostringstream filename;
    filename << "au_" << std::setfill('0') << std::setw(8) << frame_seq << ".bin";
    std::string filepath = config_.debug_dump_dir + "/" + filename.str();

    // 写入文件
    std::ofstream file(filepath, std::ios::binary);
    if (file) {
        file.write(reinterpret_cast<const char*>(au_payload.data()), au_payload.size());
        file.close();

        if (config_.verbose) {
            std::cout << "[DEBUG] Dumped AU seq=" << frame_seq
                      << " size=" << au_payload.size() << std::endl;
        }
    }

    debug_dump_count_++;

    // 达到上限时写入 manifest
    if (debug_dump_count_ >= config_.debug_dump_max) {
        write_manifest();
    }
}

void UdpSender::write_manifest() {
    if (config_.debug_dump_dir.empty() || debug_dump_manifest_.empty()) {
        return;
    }

    std::string manifest_path = config_.debug_dump_dir + "/au_manifest.txt";
    std::ofstream file(manifest_path);
    if (file) {
        for (const auto& entry : debug_dump_manifest_) {
            file << entry << std::endl;
        }
        file.close();
        std::cout << "[INFO] Wrote AU manifest: " << manifest_path << std::endl;
    }
}

} // namespace udp_video