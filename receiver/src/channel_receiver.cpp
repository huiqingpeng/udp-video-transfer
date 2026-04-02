/**
 * @file channel_receiver.cpp
 * @brief 单路接收器实现
 */

#include "channel_receiver.hpp"
#include <iostream>
#include <cstring>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <array>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>

namespace udp_video {

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// SHA256 实现（简化版，用于 AU dump）
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

namespace {

// SHA256 简化实现
class SimpleSHA256 {
private:
    uint32_t state[8];
    uint8_t buffer[64];
    uint64_t total_len;

    static const uint32_t K[64];

    static uint32_t rotr(uint32_t x, int n) {
        return (x >> n) | (x << (32 - n));
    }

    void transform(const uint8_t* block) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) |
                   (block[i*4+2] << 8) | block[i*4+3];
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i-15], 7) ^ rotr(w[i-15], 18) ^ (w[i-15] >> 3);
            uint32_t s1 = rotr(w[i-2], 17) ^ rotr(w[i-2], 19) ^ (w[i-2] >> 10);
            w[i] = w[i-16] + s0 + w[i-7] + s1;
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t temp1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = S0 + maj;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    }

public:
    SimpleSHA256() : total_len(0) {
        state[0] = 0x6a09e667; state[1] = 0xbb67ae85;
        state[2] = 0x3c6ef372; state[3] = 0xa54ff53a;
        state[4] = 0x510e527f; state[5] = 0x9b05688c;
        state[6] = 0x1f83d9ab; state[7] = 0x5be0cd19;
        memset(buffer, 0, 64);
    }

    void update(const uint8_t* data, size_t len) {
        size_t buf_pos = total_len % 64;
        total_len += len;

        while (len > 0) {
            size_t copy_len = std::min(len, 64 - buf_pos);
            memcpy(buffer + buf_pos, data, copy_len);
            data += copy_len;
            len -= copy_len;
            buf_pos += copy_len;

            if (buf_pos == 64) {
                transform(buffer);
                buf_pos = 0;
            }
        }
    }

    void final(uint8_t* hash) {
        size_t buf_pos = total_len % 64;
        uint64_t bit_len = total_len * 8;

        buffer[buf_pos++] = 0x80;
        if (buf_pos > 56) {
            while (buf_pos < 64) buffer[buf_pos++] = 0;
            transform(buffer);
            buf_pos = 0;
        }
        while (buf_pos < 56) buffer[buf_pos++] = 0;

        for (int i = 0; i < 8; ++i) {
            buffer[56 + i] = (bit_len >> (56 - i*8)) & 0xff;
        }
        transform(buffer);

        for (int i = 0; i < 8; ++i) {
            hash[i*4] = (state[i] >> 24) & 0xff;
            hash[i*4+1] = (state[i] >> 16) & 0xff;
            hash[i*4+2] = (state[i] >> 8) & 0xff;
            hash[i*4+3] = state[i] & 0xff;
        }
    }

    static std::string to_hex(const uint8_t* hash) {
        std::stringstream ss;
        for (int i = 0; i < 32; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
};

const uint32_t SimpleSHA256::K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

} // anonymous namespace

ChannelReceiver::ChannelReceiver(const ChannelConfig& config)
    : config_(config)
    , metrics_() {

    reassembly_ = std::make_unique<ReassemblyManager>(config_.channel_id, 80);
    fsm_ = std::make_unique<IdrFsm>();
}

ChannelReceiver::~ChannelReceiver() {
    stop();

    // 写入 AU dump manifest
    write_dump_manifest();

    if (socket_ >= 0) {
        close(socket_);
    }
}

bool ChannelReceiver::init() {
    // 初始化文件输出
    if (config_.mode == OutputMode::FILE || config_.mode == OutputMode::DUAL) {
        writer_ = std::make_unique<AnnexBWriter>(config_.output_dir, config_.channel_id);
    } else if (config_.mode == OutputMode::DECODE) {
        // decode 模式也需要 writer 来解析 AU
        writer_ = std::make_unique<AnnexBWriter>("", config_.channel_id);
    }

    // 初始化解码器
#ifdef HAS_GSTREAMER
    if (config_.enable_decode && (config_.mode == OutputMode::DECODE || config_.mode == OutputMode::DUAL)) {
        decoder_ = std::make_unique<GstDecoder>();
        if (!decoder_->init()) {
            std::cerr << "[CH" << (int)config_.channel_id << "] Failed to init decoder" << std::endl;
            decoder_.reset();
        }
    }
#else
    if (config_.enable_decode && (config_.mode == OutputMode::DECODE || config_.mode == OutputMode::DUAL)) {
        std::cerr << "[CH" << (int)config_.channel_id
                  << "] Warning: GStreamer not available, decode mode disabled" << std::endl;
    }
#endif

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // AU dump 目录初始化
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    if (!config_.debug_dump_au_dir.empty()) {
        std::string dump_dir = config_.debug_dump_au_dir;
        // 创建目录（如果不存在）
        if (mkdir(dump_dir.c_str(), 0755) != 0 && errno != EEXIST) {
            std::cerr << "[CH" << (int)config_.channel_id << "] Failed to create dump dir: "
                      << strerror(errno) << std::endl;
        } else {
            std::cout << "[CH" << (int)config_.channel_id << "] AU dump enabled: "
                      << dump_dir << " max=" << config_.debug_dump_max_au << std::endl;
        }
    }

    // 创建 socket
    socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_ < 0) {
        std::cerr << "[CH" << (int)config_.channel_id << "] Failed to create socket: "
                  << strerror(errno) << std::endl;
        return false;
    }

    // 设置接收缓冲区（增大到 16 MB 以减少丢包）
    int recvbuf = 16 * 1024 * 1024; // 16 MB per channel
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &recvbuf, sizeof(recvbuf)) < 0) {
        std::cerr << "[CH" << (int)config_.channel_id
                  << "] Warning: Failed to set SO_RCVBUF to " << recvbuf
                  << ", using system default" << std::endl;
    }

    // 绑定端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(CHANNEL_PORTS[config_.channel_id]);

    if (bind(socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[CH" << (int)config_.channel_id << "] Failed to bind port "
                  << CHANNEL_PORTS[config_.channel_id] << ": " << strerror(errno) << std::endl;
        close(socket_);
        socket_ = -1;
        return false;
    }

    std::cout << "[CH" << (int)config_.channel_id << "] Listening on port "
              << CHANNEL_PORTS[config_.channel_id] << std::endl;

    initialized_ = true;
    return true;
}

void ChannelReceiver::start() {
    if (!initialized_) {
        std::cerr << "[CH" << (int)config_.channel_id << "] Not initialized" << std::endl;
        return;
    }

    running_ = true;
    thread_ = std::thread(&ChannelReceiver::receive_loop, this);
}

void ChannelReceiver::stop() {
    running_ = false;

#ifdef HAS_GSTREAMER
    if (decoder_) {
        decoder_->stop();
    }
#endif

    if (thread_.joinable()) {
        thread_.join();
    }
}

void ChannelReceiver::join() {
    if (thread_.joinable()) {
        thread_.join();
    }
}

void ChannelReceiver::receive_loop() {
    uint8_t buffer[65536];
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    auto last_stats_time = std::chrono::steady_clock::now();
    auto last_cleanup_time = std::chrono::steady_clock::now();

    while (running_) {
        // 接收数据
        ssize_t recv_len = recvfrom(socket_, buffer, sizeof(buffer), 0,
                                    (struct sockaddr*)&client_addr, &addr_len);

        if (recv_len < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[CH" << (int)config_.channel_id << "] recvfrom failed: "
                      << strerror(errno) << std::endl;
            continue;
        }

        metrics_.total_packets++;
        metrics_.bytes_received += recv_len;

        // 检查最小长度
        if (static_cast<size_t>(recv_len) < HEADER_SIZE) {
            continue;
        }

        // 解析协议头
        PacketHeader header;
        ParseResult result = parse_header(buffer, header);

        if (result != ParseResult::OK) {
            handle_parse_error(result, header);
            continue;
        }

        // 验证 channel_id
        if (header.channel_id != config_.channel_id) {
            metrics_.channel_mismatch++;
            continue;
        }

        metrics_.valid_packets++;

        // 获取 payload
        const uint8_t* payload = buffer + HEADER_SIZE;
        size_t payload_len = recv_len - HEADER_SIZE;

        // AU 重组
        std::vector<uint8_t> completed_au;
        ReassemblyResult reasm_result = reassembly_->process_fragment(
            header, payload, payload_len, completed_au);

        switch (reasm_result) {
            case ReassemblyResult::COMPLETED:
                process_completed_au(completed_au, header);
                break;

            case ReassemblyResult::DUPLICATE:
                metrics_.duplicate_frag++;
                break;

            case ReassemblyResult::INVALID_OFFSET:
            case ReassemblyResult::BUFFER_OVERFLOW:
                metrics_.invalid_fragment++;
                break;

            case ReassemblyResult::IN_PROGRESS:
            case ReassemblyResult::NEW_AU_STARTED:
                break;
        }

        // 定期清理过期 AU
        auto now = std::chrono::steady_clock::now();
        auto cleanup_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_cleanup_time);
        if (cleanup_elapsed.count() >= 20) {
            uint64_t before = reassembly_->get_timeout_count();
            reassembly_->cleanup_expired();
            uint64_t after = reassembly_->get_timeout_count();

            if (after > before) {
                uint64_t timed_out = after - before;
                metrics_.au_timeout += timed_out;
                std::cout << "[CH" << (int)config_.channel_id << "] AU timeout: "
                          << timed_out << " AU(s) expired" << std::endl;

                for (uint64_t i = 0; i < timed_out; ++i) {
                    FsmState prev_state = fsm_->get_state();
                    fsm_->on_au_timeout();
                    if (prev_state == FsmState::RUNNING && fsm_->get_state() == FsmState::WAIT_IDR) {
                        metrics_.wait_idr_entries++;
                    }
                }
            }
            last_cleanup_time = now;
        }

        // 定期打印统计
        auto stats_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time);
        if (stats_elapsed.count() >= 5) {
            print_stats();
            last_stats_time = now;
        }
    }

    // 最终统计
    std::cout << "[CH" << (int)config_.channel_id << "] Stopped" << std::endl;
    print_stats();
}

void ChannelReceiver::process_completed_au(const std::vector<uint8_t>& au_data, const PacketHeader& header) {
    // AU 完整，先尝试解析
    bool parse_success = false;
    if (writer_) {
        parse_success = writer_->can_parse_au(au_data, header);
    }

    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    // AU dump（在重组完成后、Annex B 恢复前）
    // ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
    dump_au(au_data, header);

    // 检查 frame_seq 跳变
    if (fsm_->get_state() == FsmState::RUNNING) {
        if (fsm_->check_frame_seq_gap(header.frame_seq)) {
            metrics_.frame_seq_gap++;
            FsmState prev_state = fsm_->get_state();
            fsm_->on_au_timeout();
            if (prev_state == FsmState::RUNNING && fsm_->get_state() == FsmState::WAIT_IDR) {
                metrics_.wait_idr_entries++;
            }
        }
    }

    // 通过状态机处理
    bool should_output = fsm_->process_au(au_data, header, parse_success);

    if (should_output) {
        bool output_success = process_output(au_data, header);
        if (output_success) {
            metrics_.au_completed++;
            if (header.is_idr) {
                metrics_.idr_accepted++;
            }
        } else {
            metrics_.au_parse_fail++;
            fsm_->on_au_timeout();
        }
    } else {
        // 被状态机丢弃
        if (fsm_->get_state() == FsmState::WAIT_FIRST_IDR) {
            metrics_.wait_first_idr_drops++;
        } else if (fsm_->get_state() == FsmState::WAIT_IDR) {
            metrics_.wait_idr_drops++;
            if (header.is_idr) {
                metrics_.invalid_idr++;
            }
        }
    }
}

bool ChannelReceiver::process_output(const std::vector<uint8_t>& au_data, const PacketHeader& header) {
    bool success = true;

    // 文件输出
    if (writer_ && (config_.mode == OutputMode::FILE || config_.mode == OutputMode::DUAL)) {
        if (!writer_->process_au(au_data, header)) {
            success = false;
        }
    }

    // 解码输出
#ifdef HAS_GSTREAMER
    if (decoder_ && success) {
        std::vector<uint8_t> annexb_data;
        if (writer_ && writer_->convert_to_annexb(au_data, header, annexb_data)) {
            decoder_->push_annexb_data(annexb_data, header.pts);
            metrics_.frames_decoded = decoder_->get_frames_decoded();
            metrics_.decode_errors = decoder_->get_decode_errors();
            metrics_.bytes_pushed_decode += annexb_data.size();
        }
    }
#endif

    return success;
}

void ChannelReceiver::handle_parse_error(ParseResult result, const PacketHeader& header) {
    switch (result) {
        case ParseResult::INVALID_MAGIC:
            metrics_.magic_fail++;
            break;
        case ParseResult::INVALID_VERSION:
            metrics_.version_fail++;
            break;
        case ParseResult::INVALID_CHANNEL:
            metrics_.channel_mismatch++;
            break;
        case ParseResult::CRC_MISMATCH:
            metrics_.crc_fail++;
            break;
        case ParseResult::INVALID_AU_SIZE:
            metrics_.invalid_au_size++;
            break;
        case ParseResult::INVALID_FRAG_TOTAL:
            metrics_.invalid_frag_total++;
            break;
        default:
            break;
    }
}

void ChannelReceiver::print_stats() {
    if (writer_) {
        metrics_.bytes_written = writer_->get_bytes_written();
        metrics_.au_parse_fail = writer_->get_parse_fail_count();
    }
    metrics_.au_timeout = reassembly_->get_timeout_count();

    // 检查状态机恢复
    static FsmState last_states[4] = {FsmState::INIT, FsmState::INIT, FsmState::INIT, FsmState::INIT};
    FsmState current_state = fsm_->get_state();
    if (last_states[config_.channel_id] == FsmState::WAIT_IDR && current_state == FsmState::RUNNING) {
        metrics_.wait_idr_recovers++;
    }
    last_states[config_.channel_id] = current_state;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - metrics_.start_time);

    std::cout << "[CH" << (int)config_.channel_id << "] "
              << "Elapsed=" << elapsed.count() << "s "
              << "Packets=" << metrics_.total_packets.load()
              << " Valid=" << metrics_.valid_packets.load()
              << " CRC_fail=" << metrics_.crc_fail.load()
              << "\n        "
              << "au_timeout=" << metrics_.au_timeout.load()
              << " AU_completed=" << metrics_.au_completed.load()
              << " IDR=" << metrics_.idr_accepted.load()
              << "\n        "
              << "State=" << fsm_->get_state_name();

#ifdef HAS_GSTREAMER
    if (decoder_) {
        std::cout << " Decoded=" << metrics_.frames_decoded.load()
                  << " Err=" << metrics_.decode_errors.load();
    }
#endif

    std::cout << std::endl;
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// AU dump 调试功能
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

void ChannelReceiver::dump_au(const std::vector<uint8_t>& au_data, const PacketHeader& header) {
    if (config_.debug_dump_au_dir.empty() || debug_dump_count_ >= config_.debug_dump_max_au) {
        return;
    }

    // 计算 SHA256（使用简化实现）
    SimpleSHA256 sha256;
    sha256.update(au_data.data(), au_data.size());
    uint8_t hash[32];
    sha256.final(hash);
    std::string sha256_str = SimpleSHA256::to_hex(hash);

    // 文件名: au_00000000.bin
    std::stringstream filename_ss;
    filename_ss << "au_" << std::setw(8) << std::setfill('0') << header.frame_seq << ".bin";
    std::string filename = filename_ss.str();

    std::string filepath = config_.debug_dump_au_dir + "/" + filename;

    // 写入 AU payload
    std::ofstream out(filepath, std::ios::binary);
    if (out.is_open()) {
        out.write(reinterpret_cast<const char*>(au_data.data()), au_data.size());
        out.close();
    } else {
        std::cerr << "[CH" << (int)config_.channel_id << "] Failed to write dump: " << filepath << std::endl;
        return;
    }

    // 构建元信息 JSON
    std::stringstream meta_json;
    meta_json << "{"
              << "\"frame_seq\":" << header.frame_seq << ","
              << "\"is_idr\":" << (int)header.is_idr << ","
              << "\"primary_nal_type\":" << (int)header.primary_nal_type << ","
              << "\"au_nal_count\":" << header.au_nal_count << ","
              << "\"au_size\":" << au_data.size() << ","
              << "\"frag_total\":" << header.frag_total << ","
              << "\"pts\":" << header.pts << ","
              << "\"sha256\":\"" << sha256_str << "\","
              << "\"filename\":\"" << filename << "\""
              << "}";

    debug_dump_manifest_.push_back(meta_json.str());
    debug_dump_count_++;

    std::cout << "[CH" << (int)config_.channel_id << "] Dumped AU seq=" << header.frame_seq
              << " size=" << au_data.size() << " hash=" << sha256_str.substr(0, 16) << "..."
              << std::endl;

    // 达到上限时写入 manifest
    if (debug_dump_count_ >= config_.debug_dump_max_au) {
        write_dump_manifest();
    }
}

void ChannelReceiver::write_dump_manifest() {
    if (config_.debug_dump_au_dir.empty() || debug_dump_manifest_.empty()) {
        return;
    }

    std::string manifest_path = config_.debug_dump_au_dir + "/au_manifest.json";

    std::ofstream out(manifest_path);
    if (out.is_open()) {
        out << "[\n";
        for (size_t i = 0; i < debug_dump_manifest_.size(); ++i) {
            out << "  " << debug_dump_manifest_[i];
            if (i < debug_dump_manifest_.size() - 1) {
                out << ",\n";
            } else {
                out << "\n";
            }
        }
        out << "]\n";
        out.close();

        std::cout << "[CH" << (int)config_.channel_id << "] Wrote AU manifest: "
                  << manifest_path << " (" << debug_dump_manifest_.size() << " entries)" << std::endl;
    }
}

} // namespace udp_video