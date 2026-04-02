/**
 * @file packet_header.hpp
 * @brief UDP 视频传输协议头定义与解析
 *
 * 协议头结构（30 字节，全部大端序）：
 * | 字段             | 偏移 | 大小 | 说明                        |
 * | magic            | 0    | 2B   | 固定 0xAA55                 |
 * | version          | 2    | 1B   | 固定 0x02                   |
 * | channel_id       | 3    | 1B   | 0~3                         |
 * | frame_seq        | 4    | 4B   | AU粒度递增，每路独立        |
 * | frag_idx         | 8    | 2B   | 当前分片索引（0-based）     |
 * | frag_total       | 10   | 2B   | 总分片数 ceil(au_size/1440) |
 * | is_idr           | 12   | 1B   | IDR/CRA帧置1                |
 * | primary_nal_type | 13   | 1B   | 首个VCL NAL类型             |
 * | au_nal_count     | 14   | 2B   | AU内NAL数量                 |
 * | pts              | 16   | 8B   | 90kHz时间戳                 |
 * | au_size          | 24   | 4B   | AU序列化总字节数（不含头）  |
 * | header_crc       | 28   | 2B   | CRC-16/IBM，覆盖前28字节    |
 */

#ifndef PACKET_HEADER_HPP
#define PACKET_HEADER_HPP

#include <cstdint>
#include <cstddef>

namespace udp_video {

// 协议常量
constexpr uint16_t PROTOCOL_MAGIC   = 0xAA55;
constexpr uint8_t  PROTOCOL_VERSION = 0x02;
constexpr size_t   HEADER_SIZE      = 30;
constexpr size_t   FRAGMENT_PAYLOAD_SIZE = 1440;

// 安全上限
constexpr uint32_t AU_SIZE_MAX = 200 * 1024;     // 200KB
constexpr uint16_t FRAG_TOTAL_MAX = 200;          // 200 * 1440 = 288KB

// 端口映射
constexpr uint16_t CHANNEL_PORTS[4] = {5000, 5001, 5002, 5003};

/**
 * @brief 协议头结构
 */
struct PacketHeader {
    uint16_t magic;           // 0xAA55
    uint8_t  version;         // 0x02
    uint8_t  channel_id;      // 0~3
    uint32_t frame_seq;       // AU 序号
    uint16_t frag_idx;        // 当前分片索引
    uint16_t frag_total;      // 总分片数
    uint8_t  is_idr;          // 是否为 IDR 帧
    uint8_t  primary_nal_type;// 首个 VCL NAL 类型
    uint16_t au_nal_count;    // AU 内 NAL 数量
    uint64_t pts;             // 显示时间戳 (90kHz)
    uint32_t au_size;         // AU 序列化总长度
    uint16_t header_crc;      // 头部 CRC
};

/**
 * @brief 解析结果枚举
 */
enum class ParseResult {
    OK,                 // 解析成功
    INVALID_MAGIC,      // magic 错误
    INVALID_VERSION,    // version 错误
    INVALID_CHANNEL,    // channel_id 不匹配
    CRC_MISMATCH,       // CRC 校验失败
    INVALID_LENGTH,     // 长度不足
    INVALID_FRAGMENT,   // 分片字段非法
    INVALID_AU_SIZE,    // au_size 超限
    INVALID_FRAG_TOTAL, // frag_total 超限
};

/**
 * @brief 从字节流解析协议头
 * @param data 输入数据（至少 30 字节）
 * @param header 输出协议头结构
 * @return 解析结果
 */
ParseResult parse_header(const uint8_t* data, PacketHeader& header);

/**
 * @brief 获取解析结果描述字符串
 */
const char* parse_result_str(ParseResult result);

} // namespace udp_video

#endif // PACKET_HEADER_HPP