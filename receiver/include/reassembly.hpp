/**
 * @file reassembly.hpp
 * @brief AU 重组模块
 *
 * 功能：
 * - 以 (channel_id, frame_seq) 为键管理重组上下文
 * - 按 frag_idx 将 payload 写入正确偏移
 * - 检测 AU 完整性
 * - 处理超时和清理
 */

#ifndef REASSEMBLY_HPP
#define REASSEMBLY_HPP

#include "packet_header.hpp"
#include <cstdint>
#include <vector>
#include <unordered_map>
#include <memory>
#include <chrono>

namespace udp_video {

/**
 * @brief 单个 AU 重组上下文
 */
struct AuContext {
    uint32_t frame_seq;
    uint16_t frag_total;
    uint32_t au_size;
    uint64_t pts;
    uint8_t  is_idr;
    uint8_t  primary_nal_type;
    uint16_t au_nal_count;
    uint8_t  channel_id;

    std::vector<uint8_t> buffer;        // 重组缓冲区
    std::vector<bool> frag_received;    // 分片接收位图
    uint16_t received_count;            // 已接收分片数

    std::chrono::steady_clock::time_point first_frag_time; // 首分片时间

    AuContext() : received_count(0) {}

    /**
     * @brief 初始化重组上下文
     */
    void init(const PacketHeader& header);
};

/**
 * @brief 重组结果
 */
enum class ReassemblyResult {
    IN_PROGRESS,    // 重组进行中
    COMPLETED,      // AU 完整
    DUPLICATE,      // 重复分片
    INVALID_OFFSET, // 偏移非法
    BUFFER_OVERFLOW,// 缓冲区溢出
    NEW_AU_STARTED, // 新 AU 开始（旧的被丢弃）
};

/**
 * @brief AU 重组管理器（单路）
 */
class ReassemblyManager {
public:
    /**
     * @brief 构造函数
     * @param channel_id 通道 ID
     * @param timeout_ms 超时时间（毫秒）
     */
    ReassemblyManager(uint8_t channel_id, uint32_t timeout_ms = 80);

    /**
     * @brief 处理接收到的分片
     * @param header 协议头
     * @param payload 分片数据（不含协议头）
     * @param payload_len 数据长度
     * @param[out] completed_au 如果 AU 完成，输出完整数据
     * @return 重组结果
     */
    ReassemblyResult process_fragment(
        const PacketHeader& header,
        const uint8_t* payload,
        size_t payload_len,
        std::vector<uint8_t>& completed_au
    );

    /**
     * @brief 获取当前 AU 上下文
     */
    const AuContext* get_current_context() const;

    /**
     * @brief 清理过期的 AU 上下文
     */
    void cleanup_expired();

    /**
     * @brief 获取 AU 超时计数
     */
    uint64_t get_timeout_count() const { return au_timeout_count_; }

private:
    uint8_t channel_id_;
    uint32_t timeout_ms_;
    std::unordered_map<uint32_t, std::unique_ptr<AuContext>> au_map_;

    // 统计
    uint64_t au_timeout_count_ = 0;
};

} // namespace udp_video

#endif // REASSEMBLY_HPP