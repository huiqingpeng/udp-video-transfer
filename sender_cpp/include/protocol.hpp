/**
 * @file protocol.hpp
 * @brief UDP 视频传输协议 v1.1 头部序列化
 */

#ifndef PROTOCOL_HPP
#define PROTOCOL_HPP

#include <cstdint>
#include <cstddef>
#include <vector>

namespace udp_video {

// 协议常量
constexpr uint16_t PROTOCOL_MAGIC = 0xAA55;
constexpr uint8_t PROTOCOL_VERSION = 0x02;
constexpr size_t HEADER_SIZE = 30;
constexpr size_t FRAGMENT_PAYLOAD_SIZE = 1440;

// 端口映射
constexpr uint16_t CHANNEL_PORTS[4] = {5000, 5001, 5002, 5003};

/**
 * @brief 协议头结构（30 字节）
 */
struct ProtocolHeader {
    uint16_t magic = PROTOCOL_MAGIC;
    uint8_t version = PROTOCOL_VERSION;
    uint8_t channel_id = 0;
    uint32_t frame_seq = 0;
    uint16_t frag_idx = 0;
    uint16_t frag_total = 1;
    uint8_t is_idr = 0;
    uint8_t primary_nal_type = 0;
    uint16_t au_nal_count = 0;
    uint64_t pts = 0;
    uint32_t au_size = 0;
    uint16_t header_crc = 0;
};

/**
 * @brief 序列化协议头（30 字节，全部大端序）
 * @param header 协议头
 * @return 序列化后的字节
 */
std::vector<uint8_t> serialize_header(const ProtocolHeader& header);

/**
 * @brief 构建 UDP 分片
 * @param au_payload AU 数据
 * @param header_template 协议头模板
 * @return 分片列表（每个分片包含完整协议头 + payload）
 */
std::vector<std::vector<uint8_t>> build_fragments(
    const std::vector<uint8_t>& au_payload,
    const ProtocolHeader& header_template
);

} // namespace udp_video

#endif // PROTOCOL_HPP