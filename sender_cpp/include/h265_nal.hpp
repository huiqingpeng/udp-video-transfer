/**
 * @file h265_nal.hpp
 * @brief H.265 NAL 单元解析与 AU 组帧
 */

#ifndef H265_NAL_HPP
#define H265_NAL_HPP

#include <cstdint>
#include <vector>
#include <optional>
#include <string>

namespace udp_video {

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// H.265 NAL 类型定义
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

enum class H265NalType : uint8_t {
    TRAIL_N_MIN = 0,
    TRAIL_N_MAX = 8,
    TRAIL_R_MIN = 9,
    TRAIL_R_MAX = 18,
    IDR_W_RADL = 19,
    IDR_N_LP = 20,
    CRA_NUT = 21,
    RSV_IRAP_VCL_MIN = 22,
    RSV_IRAP_VCL_MAX = 31,
    VPS = 32,
    SPS = 33,
    PPS = 34,
    AUD = 35,
    SEI_PREFIX = 39,
    SEI_SUFFIX = 40
};

/**
 * @brief 判断是否为 VCL NAL 类型
 */
inline bool is_vcl_nal_type(uint8_t nal_type) {
    return nal_type <= 31;  // VCL NAL types: 0-31
}

/**
 * @brief 判断是否为 IDR/CRA NAL 类型
 */
inline bool is_idr_nal_type(uint8_t nal_type) {
    return nal_type == static_cast<uint8_t>(H265NalType::IDR_W_RADL) ||
           nal_type == static_cast<uint8_t>(H265NalType::IDR_N_LP) ||
           nal_type == static_cast<uint8_t>(H265NalType::CRA_NUT);
}

/**
 * @brief 获取 NAL 类型名称
 */
std::string get_nal_type_name(uint8_t nal_type);

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// NAL Unit 结构
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct NalUnit {
    uint8_t nal_type;
    std::vector<uint8_t> data;  // 不含起始码
    uint8_t start_code_len;     // 起始码长度 (3 或 4)

    NalUnit() : nal_type(0), start_code_len(4) {}
    NalUnit(uint8_t type, const std::vector<uint8_t>& d, uint8_t sc_len = 4)
        : nal_type(type), data(d), start_code_len(sc_len) {}
};

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Annex B 解析
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

/**
 * @brief 从指定偏移开始向后搜索起始码位置
 * @param data 数据
 * @param offset 起始偏移
 * @param out_nal_start 输出：NAL 数据开始位置（起始码之后）
 * @param out_sc_len 输出：起始码长度
 * @return 是否找到起始码
 */
bool find_start_code(const std::vector<uint8_t>& data, size_t offset,
                     size_t& out_nal_start, uint8_t& out_sc_len);

/**
 * @brief 解析 Annex B 字节流，提取所有完整的 NAL Unit
 * @param stream 输入流
 * @param parsed_len 输出：已解析的字节数
 * @return NAL 列表（只返回完整的 NAL）
 *
 * 注意：只返回完整的 NAL（后面有起始码）
 * 不完整的 NAL（在 buffer 末尾被截断）不会被返回
 */
std::vector<NalUnit> parse_nal_units(const std::vector<uint8_t>& stream, size_t& parsed_len);

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Access Unit 结构
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

struct AccessUnit {
    std::vector<NalUnit> nals;
    uint64_t pts;           // 90kHz 时间戳
    bool is_idr;
    uint8_t primary_nal_type;

    AccessUnit() : pts(0), is_idr(false), primary_nal_type(0) {}
};

/**
 * @brief 从 NAL 列表构建 AU
 * @param nals NAL 列表
 * @param pts 时间戳
 * @param cached_vps 缓存的 VPS（可选）
 * @param cached_sps 缓存的 SPS（可选）
 * @param cached_pps 缓存的 PPS（可选）
 * @return AU（如果无效返回 std::nullopt）
 */
std::optional<AccessUnit> build_au_from_nals(
    std::vector<NalUnit>& nals,
    uint64_t pts,
    const std::vector<uint8_t>* cached_vps,
    const std::vector<uint8_t>* cached_sps,
    const std::vector<uint8_t>* cached_pps
);

/**
 * @brief 序列化 AU 为协议格式
 * @param au AU
 * @return AU payload: [4B nalu_len_be][nalu_bytes] × N
 */
std::vector<uint8_t> serialize_au(const AccessUnit& au);

} // namespace udp_video

#endif // H265_NAL_HPP