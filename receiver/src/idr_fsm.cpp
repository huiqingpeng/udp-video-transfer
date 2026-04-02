/**
 * @file idr_fsm.cpp
 * @brief IDR 恢复状态机实现
 */

#include "idr_fsm.hpp"
#include <iostream>
#include <cstring>

namespace udp_video {

// H.265 NAL 类型
constexpr uint8_t NAL_TYPE_VPS = 32;
constexpr uint8_t NAL_TYPE_SPS = 33;
constexpr uint8_t NAL_TYPE_PPS = 34;
constexpr uint8_t NAL_TYPE_IDR_W_RADL = 19;
constexpr uint8_t NAL_TYPE_IDR_N_LP = 20;
constexpr uint8_t NAL_TYPE_CRA_NUT = 21;

IdrFsm::IdrFsm()
    : state_(FsmState::INIT)
    , last_frame_seq_(0)
    , has_last_frame_seq_(false)
    , wait_idr_reason_(WaitIdrReason::NONE) {

    // 启动后立即进入 WAIT_FIRST_IDR
    state_ = FsmState::WAIT_FIRST_IDR;
    std::cout << "[FSM] Enter WAIT_FIRST_IDR" << std::endl;
}

const char* IdrFsm::get_state_name() const {
    switch (state_) {
        case FsmState::INIT: return "INIT";
        case FsmState::WAIT_FIRST_IDR: return "WAIT_FIRST_IDR";
        case FsmState::RUNNING: return "RUNNING";
        case FsmState::WAIT_IDR: return "WAIT_IDR";
        default: return "UNKNOWN";
    }
}

IdrCheckResult IdrFsm::check_idr_validity(const std::vector<uint8_t>& au_data) {
    IdrCheckResult result = {false, false, false, false, false, 0};

    size_t offset = 0;
    const size_t total_size = au_data.size();

    while (offset + 4 <= total_size) {
        // 读取 NAL 长度
        uint32_t nal_len = (static_cast<uint32_t>(au_data[offset]) << 24) |
                           (static_cast<uint32_t>(au_data[offset + 1]) << 16) |
                           (static_cast<uint32_t>(au_data[offset + 2]) << 8) |
                           static_cast<uint32_t>(au_data[offset + 3]);
        offset += 4;

        if (nal_len == 0 || offset + nal_len > total_size) {
            break;
        }

        // 获取 NAL 类型
        uint8_t nal_type = (au_data[offset] >> 1) & 0x3F;

        // 检查各种 NAL 类型
        if (nal_type == NAL_TYPE_VPS) {
            result.has_vps = true;
        } else if (nal_type == NAL_TYPE_SPS) {
            result.has_sps = true;
        } else if (nal_type == NAL_TYPE_PPS) {
            result.has_pps = true;
        } else if (nal_type == NAL_TYPE_IDR_W_RADL ||
                   nal_type == NAL_TYPE_IDR_N_LP ||
                   nal_type == NAL_TYPE_CRA_NUT) {
            result.has_irap_vcl = true;
            result.irap_type = nal_type;
        }

        offset += nal_len;
    }

    // 判断合法性：必须有 VPS + SPS + PPS + IRAP VCL
    result.is_valid = result.has_vps && result.has_sps &&
                      result.has_pps && result.has_irap_vcl;

    return result;
}

void IdrFsm::enter_wait_idr(WaitIdrReason reason) {
    if (state_ == FsmState::WAIT_IDR) {
        return;  // 已经在 WAIT_IDR
    }

    state_ = FsmState::WAIT_IDR;
    wait_idr_reason_ = reason;

    const char* reason_str = "";
    switch (reason) {
        case WaitIdrReason::AU_TIMEOUT:
            reason_str = "AU timeout";
            break;
        case WaitIdrReason::AU_PARSE_FAIL:
            reason_str = "AU parse fail";
            break;
        case WaitIdrReason::FRAME_SEQ_GAP:
            reason_str = "frame_seq gap";
            break;
        case WaitIdrReason::INVALID_IDR:
            reason_str = "invalid IDR (missing parameter sets)";
            break;
        default:
            reason_str = "unknown";
    }

    std::cout << "[FSM] Enter WAIT_IDR (reason: " << reason_str << ")" << std::endl;
}

void IdrFsm::try_recover(const PacketHeader& header, const IdrCheckResult& check) {
    if (check.is_valid) {
        state_ = FsmState::RUNNING;
        wait_idr_reason_ = WaitIdrReason::NONE;
        has_last_frame_seq_ = true;
        last_frame_seq_ = header.frame_seq;

        const char* state_before = (state_ == FsmState::WAIT_FIRST_IDR) ?
                                   "WAIT_FIRST_IDR" : "WAIT_IDR";

        std::cout << "[FSM] Recover to RUNNING from " << state_before
                  << " (IDR frame_seq=" << header.frame_seq
                  << " IRAP_type=" << (int)check.irap_type << ")" << std::endl;
    } else {
        // IDR 不合法，保持当前状态
        std::cout << "[FSM] Invalid IDR (VPS=" << check.has_vps
                  << " SPS=" << check.has_sps
                  << " PPS=" << check.has_pps
                  << " IRAP=" << check.has_irap_vcl << ")" << std::endl;
    }
}

bool IdrFsm::process_au(const std::vector<uint8_t>& au_data,
                        const PacketHeader& header,
                        bool parse_success) {
    switch (state_) {
        case FsmState::WAIT_FIRST_IDR: {
            // 只接受合法的 IDR
            if (!header.is_idr) {
                std::cout << "[FSM] WAIT_FIRST_IDR: drop non-IDR frame_seq="
                          << header.frame_seq << std::endl;
                return false;
            }

            if (!parse_success) {
                std::cout << "[FSM] WAIT_FIRST_IDR: IDR parse failed frame_seq="
                          << header.frame_seq << std::endl;
                return false;
            }

            IdrCheckResult check = check_idr_validity(au_data);
            try_recover(header, check);
            return check.is_valid;
        }

        case FsmState::RUNNING: {
            // 检查是否为非法 IDR
            if (header.is_idr && parse_success) {
                IdrCheckResult check = check_idr_validity(au_data);
                if (!check.is_valid) {
                    // 非法 IDR，进入 WAIT_IDR
                    enter_wait_idr(WaitIdrReason::INVALID_IDR);
                    return false;
                }
            }

            // 检查解析失败
            if (!parse_success) {
                enter_wait_idr(WaitIdrReason::AU_PARSE_FAIL);
                return false;
            }

            // 更新 frame_seq
            has_last_frame_seq_ = true;
            last_frame_seq_ = header.frame_seq;
            return true;
        }

        case FsmState::WAIT_IDR: {
            // 只接受合法的 IDR
            if (!header.is_idr) {
                // 静默丢弃非 IDR（避免日志过多）
                return false;
            }

            if (!parse_success) {
                std::cout << "[FSM] WAIT_IDR: IDR parse failed frame_seq="
                          << header.frame_seq << std::endl;
                return false;
            }

            IdrCheckResult check = check_idr_validity(au_data);
            try_recover(header, check);
            return check.is_valid;
        }

        default:
            return false;
    }
}

void IdrFsm::on_au_timeout() {
    if (state_ == FsmState::RUNNING) {
        enter_wait_idr(WaitIdrReason::AU_TIMEOUT);
    }
    // WAIT_FIRST_IDR 和 WAIT_IDR 不需要额外处理
}

bool IdrFsm::check_frame_seq_gap(uint32_t new_seq) {
    if (!has_last_frame_seq_) {
        return false;  // 没有上一帧，无法判断
    }

    // 简单的顺序检查：期望 new_seq == last_frame_seq + 1
    // TODO: 处理 uint32 回绕
    uint32_t expected_seq = last_frame_seq_ + 1;

    if (new_seq != expected_seq) {
        int32_t gap = static_cast<int32_t>(new_seq - expected_seq);
        if (gap > 0) {  // 正向跳变（丢帧）
            std::cout << "[FSM] frame_seq gap detected: expected " << expected_seq
                      << " got " << new_seq << " (gap=" << gap << ")" << std::endl;
            return true;
        }
        // 负向跳变可能是回绕或乱序，暂时忽略
    }

    return false;
}

} // namespace udp_video