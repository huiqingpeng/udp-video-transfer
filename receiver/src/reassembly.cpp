/**
 * @file reassembly.cpp
 * @brief AU 重组实现
 */

#include "reassembly.hpp"
#include "crc16.hpp"
#include <algorithm>
#include <iostream>

namespace udp_video {

void AuContext::init(const PacketHeader& header) {
    frame_seq = header.frame_seq;
    frag_total = header.frag_total;
    au_size = header.au_size;
    pts = header.pts;
    is_idr = header.is_idr;
    primary_nal_type = header.primary_nal_type;
    au_nal_count = header.au_nal_count;
    channel_id = header.channel_id;

    // 分配缓冲区
    buffer.resize(au_size);
    frag_received.assign(frag_total, false);
    received_count = 0;
    first_frag_time = std::chrono::steady_clock::now();
}

ReassemblyManager::ReassemblyManager(uint8_t channel_id, uint32_t timeout_ms)
    : channel_id_(channel_id), timeout_ms_(timeout_ms) {
}

ReassemblyResult ReassemblyManager::process_fragment(
    const PacketHeader& header,
    const uint8_t* payload,
    size_t payload_len,
    std::vector<uint8_t>& completed_au
) {
    // 检查 channel_id
    if (header.channel_id != channel_id_) {
        return ReassemblyResult::INVALID_OFFSET;
    }

    // 查找或创建 AU 上下文
    auto it = au_map_.find(header.frame_seq);
    if (it == au_map_.end()) {
        // 新 AU
        auto ctx = std::make_unique<AuContext>();
        ctx->init(header);
        au_map_[header.frame_seq] = std::move(ctx);
        it = au_map_.find(header.frame_seq);
    }

    AuContext& ctx = *it->second;

    // 检查分片索引有效性
    if (header.frag_idx >= ctx.frag_total) {
        return ReassemblyResult::INVALID_OFFSET;
    }

    // 检查重复
    if (ctx.frag_received[header.frag_idx]) {
        return ReassemblyResult::DUPLICATE;
    }

    // 计算偏移
    size_t offset = static_cast<size_t>(header.frag_idx) * FRAGMENT_PAYLOAD_SIZE;

    // 检查缓冲区边界
    if (offset + payload_len > ctx.au_size) {
        return ReassemblyResult::BUFFER_OVERFLOW;
    }

    // 检查分片长度
    bool is_last_fragment = (header.frag_idx == ctx.frag_total - 1);
    if (!is_last_fragment && payload_len != FRAGMENT_PAYLOAD_SIZE) {
        // 非最后一个分片必须是 1440 字节
        return ReassemblyResult::INVALID_OFFSET;
    }
    if (is_last_fragment && (payload_len == 0 || payload_len > FRAGMENT_PAYLOAD_SIZE)) {
        // 最后一个分片必须在 1-1440 字节
        return ReassemblyResult::INVALID_OFFSET;
    }

    // 写入数据
    std::copy(payload, payload + payload_len, ctx.buffer.begin() + offset);
    ctx.frag_received[header.frag_idx] = true;
    ctx.received_count++;

    // 检查是否完整
    if (ctx.received_count == ctx.frag_total) {
        completed_au = std::move(ctx.buffer);
        au_map_.erase(it);
        return ReassemblyResult::COMPLETED;
    }

    return ReassemblyResult::IN_PROGRESS;
}

const AuContext* ReassemblyManager::get_current_context() const {
    if (au_map_.empty()) {
        return nullptr;
    }
    return au_map_.begin()->second.get();
}

void ReassemblyManager::cleanup_expired() {
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(timeout_ms_);

    for (auto it = au_map_.begin(); it != au_map_.end(); ) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - it->second->first_frag_time);
        if (elapsed > timeout) {
            std::cerr << "[WARN] AU timeout: frame_seq=" << it->second->frame_seq
                      << " received=" << it->second->received_count
                      << "/" << it->second->frag_total << std::endl;
            it = au_map_.erase(it);
            au_timeout_count_++;
        } else {
            ++it;
        }
    }
}

} // namespace udp_video