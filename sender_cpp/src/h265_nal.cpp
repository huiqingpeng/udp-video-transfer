/**
 * @file h265_nal.cpp
 * @brief H.265 NAL 单元解析与 AU 组帧实现
 */

#include "h265_nal.hpp"
#include <algorithm>

namespace udp_video {

std::string get_nal_type_name(uint8_t nal_type) {
    switch (nal_type) {
        case 32: return "VPS";
        case 33: return "SPS";
        case 34: return "PPS";
        case 35: return "AUD";
        case 39: return "SEI_PREFIX";
        case 40: return "SEI_SUFFIX";
        case 19: return "IDR_W_RADL";
        case 20: return "IDR_N_LP";
        case 21: return "CRA_NUT";
        default:
            if (nal_type <= 8) return "TRAIL_N";
            if (nal_type >= 9 && nal_type <= 18) return "TRAIL_R";
            if (nal_type >= 22 && nal_type <= 31) return "RSV_IRAP_VCL";
            return "UNKNOWN(" + std::to_string(nal_type) + ")";
    }
}

bool find_start_code(const std::vector<uint8_t>& data, size_t offset,
                     size_t& out_nal_start, uint8_t& out_sc_len) {
    while (offset < data.size() - 2) {
        // 先尝试匹配 4 字节起始码
        if (offset + 4 <= data.size()) {
            if (data[offset] == 0x00 && data[offset + 1] == 0x00 &&
                data[offset + 2] == 0x00 && data[offset + 3] == 0x01) {
                out_nal_start = offset + 4;
                out_sc_len = 4;
                return true;
            }
        }
        // 再尝试匹配 3 字节起始码
        if (offset + 3 <= data.size()) {
            if (data[offset] == 0x00 && data[offset + 1] == 0x00 &&
                data[offset + 2] == 0x01) {
                out_nal_start = offset + 3;
                out_sc_len = 3;
                return true;
            }
        }
        offset++;
    }
    return false;
}

std::vector<NalUnit> parse_nal_units(const std::vector<uint8_t>& stream, size_t& parsed_len) {
    std::vector<NalUnit> nals;
    size_t offset = 0;
    parsed_len = 0;

    while (offset < stream.size()) {
        // 查找起始码
        size_t nal_start = 0;
        uint8_t sc_len = 0;

        if (!find_start_code(stream, offset, nal_start, sc_len)) {
            // 未找到起始码
            break;
        }

        // 查找下一个起始码
        size_t next_nal_start = 0;
        uint8_t next_sc_len = 0;

        if (!find_start_code(stream, nal_start + 1, next_nal_start, next_sc_len)) {
            // 没有找到下一个起始码，这个 NAL 可能不完整
            // 不返回它，让调用者读取更多数据
            break;
        }

        // 找到下一个起始码，当前 NAL 是完整的
        size_t nal_end = next_nal_start - next_sc_len;

        if (nal_start < nal_end && nal_end <= stream.size()) {
            // 提取 NAL 数据
            std::vector<uint8_t> nal_data(stream.begin() + nal_start, stream.begin() + nal_end);

            if (!nal_data.empty()) {
                // 解析 NAL type: (nal[0] >> 1) & 0x3F
                uint8_t nal_type = (nal_data[0] >> 1) & 0x3F;
                nals.emplace_back(nal_type, nal_data, sc_len);
            }
        }

        offset = nal_end;
    }

    // 计算已解析的字节数
    parsed_len = offset;

    return nals;
}

std::optional<AccessUnit> build_au_from_nals(
    std::vector<NalUnit>& nals,
    uint64_t pts,
    const std::vector<uint8_t>* cached_vps,
    const std::vector<uint8_t>* cached_sps,
    const std::vector<uint8_t>* cached_pps
) {
    if (nals.empty()) {
        return std::nullopt;
    }

    // 找到首个 VCL NAL
    const NalUnit* first_vcl_nal = nullptr;
    for (const auto& nal : nals) {
        if (is_vcl_nal_type(nal.nal_type)) {
            first_vcl_nal = &nal;
            break;
        }
    }

    if (first_vcl_nal == nullptr) {
        // 无 VCL NAL，这不是有效 AU
        return std::nullopt;
    }

    // 判断是否为 IDR/CRA
    bool is_idr = is_idr_nal_type(first_vcl_nal->nal_type);

    // 检查参数集
    bool has_vps = false, has_sps = false, has_pps = false;
    const NalUnit* aud_nal = nullptr;

    for (const auto& nal : nals) {
        if (nal.nal_type == static_cast<uint8_t>(H265NalType::VPS)) has_vps = true;
        if (nal.nal_type == static_cast<uint8_t>(H265NalType::SPS)) has_sps = true;
        if (nal.nal_type == static_cast<uint8_t>(H265NalType::PPS)) has_pps = true;
        if (nal.nal_type == static_cast<uint8_t>(H265NalType::AUD)) aud_nal = &nal;
    }

    // 构建最终 NAL 列表
    std::vector<NalUnit> final_nals;

    // 1. AUD 作为第一个 NAL
    if (aud_nal) {
        final_nals.push_back(*aud_nal);
    }

    // 2. VPS/SPS/PPS 参数集
    for (const auto& nal : nals) {
        if (nal.nal_type == static_cast<uint8_t>(H265NalType::VPS) ||
            nal.nal_type == static_cast<uint8_t>(H265NalType::SPS) ||
            nal.nal_type == static_cast<uint8_t>(H265NalType::PPS)) {
            final_nals.push_back(nal);
        }
    }

    // 3. IDR AU 需要完整参数集，缺少时注入缓存
    if (is_idr) {
        if (!has_vps && cached_vps && !cached_vps->empty()) {
            final_nals.emplace_back(static_cast<uint8_t>(H265NalType::VPS), *cached_vps, 4);
        }
        if (!has_sps && cached_sps && !cached_sps->empty()) {
            final_nals.emplace_back(static_cast<uint8_t>(H265NalType::SPS), *cached_sps, 4);
        }
        if (!has_pps && cached_pps && !cached_pps->empty()) {
            final_nals.emplace_back(static_cast<uint8_t>(H265NalType::PPS), *cached_pps, 4);
        }
    }

    // 4. VCL 和其他 NAL
    for (const auto& nal : nals) {
        if (nal.nal_type == static_cast<uint8_t>(H265NalType::AUD)) continue;
        if (nal.nal_type == static_cast<uint8_t>(H265NalType::VPS) ||
            nal.nal_type == static_cast<uint8_t>(H265NalType::SPS) ||
            nal.nal_type == static_cast<uint8_t>(H265NalType::PPS)) continue;
        final_nals.push_back(nal);
    }

    AccessUnit au;
    au.nals = std::move(final_nals);
    au.pts = pts;
    au.is_idr = is_idr;
    au.primary_nal_type = first_vcl_nal->nal_type;

    return au;
}

std::vector<uint8_t> serialize_au(const AccessUnit& au) {
    std::vector<uint8_t> payload;

    for (const auto& nal : au.nals) {
        // 4 字节大端长度 + NAL 数据（不含起始码）
        uint32_t nalu_len = static_cast<uint32_t>(nal.data.size());

        // 大端序写入
        payload.push_back(static_cast<uint8_t>((nalu_len >> 24) & 0xFF));
        payload.push_back(static_cast<uint8_t>((nalu_len >> 16) & 0xFF));
        payload.push_back(static_cast<uint8_t>((nalu_len >> 8) & 0xFF));
        payload.push_back(static_cast<uint8_t>(nalu_len & 0xFF));

        // NAL 数据
        payload.insert(payload.end(), nal.data.begin(), nal.data.end());
    }

    return payload;
}

} // namespace udp_video