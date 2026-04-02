/**
 * @file packet_header.cpp
 * @brief 协议头解析实现
 */

#include "packet_header.hpp"
#include "crc16.hpp"
#include <cstring>

namespace udp_video {

ParseResult parse_header(const uint8_t* data, PacketHeader& header) {
    // 最小长度检查由调用者完成

    // 解析字段（大端序）
    header.magic = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    header.version = data[2];
    header.channel_id = data[3];
    header.frame_seq = (static_cast<uint32_t>(data[4]) << 24) |
                       (static_cast<uint32_t>(data[5]) << 16) |
                       (static_cast<uint32_t>(data[6]) << 8) |
                       data[7];
    header.frag_idx = (static_cast<uint16_t>(data[8]) << 8) | data[9];
    header.frag_total = (static_cast<uint16_t>(data[10]) << 8) | data[11];
    header.is_idr = data[12];
    header.primary_nal_type = data[13];
    header.au_nal_count = (static_cast<uint16_t>(data[14]) << 8) | data[15];

    // PTS (8 字节)
    header.pts = (static_cast<uint64_t>(data[16]) << 56) |
                 (static_cast<uint64_t>(data[17]) << 48) |
                 (static_cast<uint64_t>(data[18]) << 40) |
                 (static_cast<uint64_t>(data[19]) << 32) |
                 (static_cast<uint64_t>(data[20]) << 24) |
                 (static_cast<uint64_t>(data[21]) << 16) |
                 (static_cast<uint64_t>(data[22]) << 8) |
                 data[23];

    header.au_size = (static_cast<uint32_t>(data[24]) << 24) |
                     (static_cast<uint32_t>(data[25]) << 16) |
                     (static_cast<uint32_t>(data[26]) << 8) |
                     data[27];

    header.header_crc = (static_cast<uint16_t>(data[28]) << 8) | data[29];

    // 校验 magic
    if (header.magic != PROTOCOL_MAGIC) {
        return ParseResult::INVALID_MAGIC;
    }

    // 校验 version
    if (header.version != PROTOCOL_VERSION) {
        return ParseResult::INVALID_VERSION;
    }

    // 校验 channel_id（0-3）
    if (header.channel_id > 3) {
        return ParseResult::INVALID_CHANNEL;
    }

    // 校验分片字段
    if (header.frag_total == 0 || header.frag_idx >= header.frag_total) {
        return ParseResult::INVALID_FRAGMENT;
    }

    // 校验 CRC
    uint16_t computed_crc = crc16_ibm(data, 28);
    if (computed_crc != header.header_crc) {
        return ParseResult::CRC_MISMATCH;
    }

    // 校验 au_size 上限
    if (header.au_size > AU_SIZE_MAX) {
        return ParseResult::INVALID_AU_SIZE;
    }

    // 校验 frag_total 上限
    if (header.frag_total > FRAG_TOTAL_MAX) {
        return ParseResult::INVALID_FRAG_TOTAL;
    }

    return ParseResult::OK;
}

const char* parse_result_str(ParseResult result) {
    switch (result) {
        case ParseResult::OK: return "OK";
        case ParseResult::INVALID_MAGIC: return "Invalid magic";
        case ParseResult::INVALID_VERSION: return "Invalid version";
        case ParseResult::INVALID_CHANNEL: return "Invalid channel";
        case ParseResult::CRC_MISMATCH: return "CRC mismatch";
        case ParseResult::INVALID_LENGTH: return "Invalid length";
        case ParseResult::INVALID_FRAGMENT: return "Invalid fragment";
        case ParseResult::INVALID_AU_SIZE: return "Invalid au_size (exceeds limit)";
        case ParseResult::INVALID_FRAG_TOTAL: return "Invalid frag_total (exceeds limit)";
        default: return "Unknown error";
    }
}

} // namespace udp_video