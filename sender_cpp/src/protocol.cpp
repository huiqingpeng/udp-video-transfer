/**
 * @file protocol.cpp
 * @brief UDP 视频传输协议 v1.1 头部序列化实现
 */

#include "protocol.hpp"
#include "crc16.hpp"

#include <cstddef>
#include <algorithm>

namespace udp_video {

std::vector<uint8_t> serialize_header(const ProtocolHeader& header) {
    std::vector<uint8_t> bytes;
    bytes.reserve(HEADER_SIZE);

    // 前 28 字节（不含 CRC），大端序
    // magic (2B)
    bytes.push_back(static_cast<uint8_t>((header.magic >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(header.magic & 0xFF));

    // version (1B)
    bytes.push_back(header.version);

    // channel_id (1B)
    bytes.push_back(header.channel_id);

    // frame_seq (4B)
    bytes.push_back(static_cast<uint8_t>((header.frame_seq >> 24) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.frame_seq >> 16) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.frame_seq >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(header.frame_seq & 0xFF));

    // frag_idx (2B)
    bytes.push_back(static_cast<uint8_t>((header.frag_idx >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(header.frag_idx & 0xFF));

    // frag_total (2B)
    bytes.push_back(static_cast<uint8_t>((header.frag_total >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(header.frag_total & 0xFF));

    // is_idr (1B)
    bytes.push_back(header.is_idr);

    // primary_nal_type (1B)
    bytes.push_back(header.primary_nal_type);

    // au_nal_count (2B)
    bytes.push_back(static_cast<uint8_t>((header.au_nal_count >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(header.au_nal_count & 0xFF));

    // pts (8B)
    bytes.push_back(static_cast<uint8_t>((header.pts >> 56) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.pts >> 48) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.pts >> 40) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.pts >> 32) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.pts >> 24) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.pts >> 16) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.pts >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(header.pts & 0xFF));

    // au_size (4B)
    bytes.push_back(static_cast<uint8_t>((header.au_size >> 24) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.au_size >> 16) & 0xFF));
    bytes.push_back(static_cast<uint8_t>((header.au_size >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(header.au_size & 0xFF));

    // 计算 CRC（覆盖前 28 字节）
    uint16_t crc = crc16_ibm(bytes);

    // CRC (2B)
    bytes.push_back(static_cast<uint8_t>((crc >> 8) & 0xFF));
    bytes.push_back(static_cast<uint8_t>(crc & 0xFF));

    return bytes;
}

std::vector<std::vector<uint8_t>> build_fragments(
    const std::vector<uint8_t>& au_payload,
    const ProtocolHeader& header_template
) {
    std::vector<std::vector<uint8_t>> fragments;

    size_t au_size = au_payload.size();
    uint16_t frag_total = static_cast<uint16_t>((au_size + FRAGMENT_PAYLOAD_SIZE - 1) / FRAGMENT_PAYLOAD_SIZE);
    if (frag_total == 0) frag_total = 1;

    for (uint16_t frag_idx = 0; frag_idx < frag_total; frag_idx++) {
        // 计算分片偏移和长度
        size_t offset = static_cast<size_t>(frag_idx) * FRAGMENT_PAYLOAD_SIZE;
        size_t end_offset = std::min(offset + FRAGMENT_PAYLOAD_SIZE, au_size);

        // 构建协议头
        ProtocolHeader header = header_template;
        header.frame_seq = header_template.frame_seq;
        header.frag_idx = frag_idx;
        header.frag_total = frag_total;
        header.au_size = static_cast<uint32_t>(au_size);

        // 序列化头
        std::vector<uint8_t> packet = serialize_header(header);

        // 添加 payload
        packet.insert(packet.end(),
                      au_payload.begin() + offset,
                      au_payload.begin() + end_offset);

        fragments.push_back(std::move(packet));
    }

    return fragments;
}

} // namespace udp_video