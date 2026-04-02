/**
 * @file idr_fsm.hpp
 * @brief IDR 恢复状态机
 *
 * 状态：INIT -> WAIT_FIRST_IDR -> RUNNING -> WAIT_IDR
 *
 * 状态转换：
 * - 启动后进入 WAIT_FIRST_IDR
 * - WAIT_FIRST_IDR 收到合法 IDR -> RUNNING
 * - RUNNING 出现异常 -> WAIT_IDR
 * - WAIT_IDR 收到合法 IDR -> RUNNING
 */

#ifndef IDR_FSM_HPP
#define IDR_FSM_HPP

#include "packet_header.hpp"
#include <cstdint>
#include <vector>

namespace udp_video {

/**
 * @brief 状态机状态
 */
enum class FsmState {
    INIT,
    WAIT_FIRST_IDR,
    RUNNING,
    WAIT_IDR
};

/**
 * @brief 进入 WAIT_IDR 的原因
 */
enum class WaitIdrReason {
    NONE,
    AU_TIMEOUT,          // AU 重组超时
    AU_PARSE_FAIL,       // AU 解析失败
    FRAME_SEQ_GAP,       // frame_seq 跳变
    INVALID_IDR          // 非法 IDR（参数集缺失）
};

/**
 * @brief IDR 合法性检查结果
 */
struct IdrCheckResult {
    bool is_valid;
    bool has_vps;
    bool has_sps;
    bool has_pps;
    bool has_irap_vcl;
    uint8_t irap_type;  // IRAP NAL 类型 (19/20/21)
};

/**
 * @brief IDR 恢复状态机
 */
class IdrFsm {
public:
    IdrFsm();

    /**
     * @brief 获取当前状态
     */
    FsmState get_state() const { return state_; }

    /**
     * @brief 处理完整的 AU
     * @param au_data AU 序列化数据
     * @param header 协议头
     * @param parse_success AU 是否解析成功
     * @return true 如果应该输出该 AU
     */
    bool process_au(const std::vector<uint8_t>& au_data,
                    const PacketHeader& header,
                    bool parse_success);

    /**
     * @brief 处理 AU 超时事件
     */
    void on_au_timeout();

    /**
     * @brief 检查 frame_seq 是否有跳变
     * @param new_seq 新收到的 frame_seq
     * @return true 如果有跳变
     */
    bool check_frame_seq_gap(uint32_t new_seq);

    /**
     * @brief 获取上一个成功的 frame_seq
     */
    uint32_t get_last_frame_seq() const { return last_frame_seq_; }

    /**
     * @brief 获取进入 WAIT_IDR 的原因
     */
    WaitIdrReason get_wait_idr_reason() const { return wait_idr_reason_; }

    /**
     * @brief 获取状态名称
     */
    const char* get_state_name() const;

private:
    /**
     * @brief 检查 IDR AU 是否合法
     */
    IdrCheckResult check_idr_validity(const std::vector<uint8_t>& au_data);

    /**
     * @brief 进入 WAIT_IDR 状态
     */
    void enter_wait_idr(WaitIdrReason reason);

    /**
     * @brief 尝试恢复到 RUNNING
     */
    void try_recover(const PacketHeader& header, const IdrCheckResult& check);

    FsmState state_;
    uint32_t last_frame_seq_;
    bool has_last_frame_seq_;
    WaitIdrReason wait_idr_reason_;
};

} // namespace udp_video

#endif // IDR_FSM_HPP