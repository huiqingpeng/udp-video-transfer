/**
 * @file annexb_writer.cpp
 * @brief Annex B 格式恢复与文件输出实现
 */

#include "annexb_writer.hpp"
#include <iostream>
#include <cstring>

namespace udp_video {

// Annex B 起始码
const uint8_t ANNEXB_START_CODE[4] = {0x00, 0x00, 0x00, 0x01};

AnnexBWriter::AnnexBWriter(const std::string& output_dir, uint8_t channel_id)
    : channel_id_(channel_id) {
    // 构建输出文件路径（如果 output_dir 非空）
    if (!output_dir.empty()) {
        output_path_ = output_dir + "/channel" + std::to_string(channel_id) + ".h265";

        // 打开文件
        output_file_.open(output_path_, std::ios::binary | std::ios::out);
        if (!output_file_.is_open()) {
            std::cerr << "[ERROR] Failed to open output file: " << output_path_ << std::endl;
        } else {
            std::cout << "[INFO] Output file: " << output_path_ << std::endl;
        }
    }
    // 如果 output_dir 为空，则不创建文件（仅用于解析 AU）
}

AnnexBWriter::~AnnexBWriter() {
    if (output_file_.is_open()) {
        output_file_.close();
    }
}

bool AnnexBWriter::process_au(const std::vector<uint8_t>& au_data, const PacketHeader& header) {
    // 如果文件未打开，只做解析检查
    if (!output_file_.is_open()) {
        // 检查 AU 是否可解析
        std::vector<uint8_t> temp;
        return convert_to_annexb(au_data, header, temp);
    }

    bool success = parse_and_write(au_data, header);

    if (success) {
        au_count_++;
        if (header.is_idr) {
            idr_count_++;
        }
    } else {
        parse_fail_count_++;
    }

    return success;
}

bool AnnexBWriter::can_parse_au(const std::vector<uint8_t>& au_data, const PacketHeader& header) {
    // AU 格式：[4B nalu_len_be][nalu_bytes] × au_nal_count
    size_t offset = 0;
    const size_t total_size = au_data.size();

    // 最小大小检查：每个 NAL 至少 4 字节长度前缀
    if (total_size < static_cast<size_t>(header.au_nal_count) * 4) {
        return false;
    }

    for (uint16_t i = 0; i < header.au_nal_count; ++i) {
        // 检查是否有足够的数据读取长度
        if (offset + 4 > total_size) {
            return false;
        }

        // 读取 NAL 长度（大端序）
        uint32_t nal_len = (static_cast<uint32_t>(au_data[offset]) << 24) |
                           (static_cast<uint32_t>(au_data[offset + 1]) << 16) |
                           (static_cast<uint32_t>(au_data[offset + 2]) << 8) |
                           static_cast<uint32_t>(au_data[offset + 3]);
        offset += 4;

        // 检查长度有效性
        if (nal_len == 0 || offset + nal_len > total_size) {
            return false;
        }

        offset += nal_len;
    }

    // 检查是否恰好消费完所有数据
    if (offset != header.au_size) {
        return false;
    }

    return true;
}

bool AnnexBWriter::convert_to_annexb(const std::vector<uint8_t>& au_data, const PacketHeader& header,
                                      std::vector<uint8_t>& annexb_data) {
    // 清空输出
    annexb_data.clear();

    // AU 格式：[4B nalu_len_be][nalu_bytes] × au_nal_count
    size_t offset = 0;
    const size_t total_size = au_data.size();

    // 最小大小检查
    if (total_size < static_cast<size_t>(header.au_nal_count) * 4) {
        return false;
    }

    for (uint16_t i = 0; i < header.au_nal_count; ++i) {
        if (offset + 4 > total_size) {
            return false;
        }

        // 读取 NAL 长度（大端序）
        uint32_t nal_len = (static_cast<uint32_t>(au_data[offset]) << 24) |
                           (static_cast<uint32_t>(au_data[offset + 1]) << 16) |
                           (static_cast<uint32_t>(au_data[offset + 2]) << 8) |
                           static_cast<uint32_t>(au_data[offset + 3]);
        offset += 4;

        if (nal_len == 0 || offset + nal_len > total_size) {
            return false;
        }

        // 写入 Annex B 起始码
        annexb_data.insert(annexb_data.end(), ANNEXB_START_CODE, ANNEXB_START_CODE + 4);

        // 写入 NAL 数据
        annexb_data.insert(annexb_data.end(), au_data.data() + offset, au_data.data() + offset + nal_len);

        offset += nal_len;
    }

    // 检查是否恰好消费完所有数据
    if (offset != header.au_size) {
        annexb_data.clear();
        return false;
    }

    return true;
}

bool AnnexBWriter::parse_and_write(const std::vector<uint8_t>& au_data, const PacketHeader& header) {
    // AU 格式：[4B nalu_len_be][nalu_bytes] × au_nal_count
    size_t offset = 0;
    const size_t total_size = au_data.size();

    // 最小大小检查：每个 NAL 至少 4 字节长度前缀
    if (total_size < static_cast<size_t>(header.au_nal_count) * 4) {
        std::cerr << "[WARN] AU too small for " << header.au_nal_count << " NALs" << std::endl;
        return false;
    }

    for (uint16_t i = 0; i < header.au_nal_count; ++i) {
        // 检查是否有足够的数据读取长度
        if (offset + 4 > total_size) {
            std::cerr << "[WARN] Unexpected end of AU at NAL " << i << std::endl;
            return false;
        }

        // 读取 NAL 长度（大端序）
        uint32_t nal_len = (static_cast<uint32_t>(au_data[offset]) << 24) |
                           (static_cast<uint32_t>(au_data[offset + 1]) << 16) |
                           (static_cast<uint32_t>(au_data[offset + 2]) << 8) |
                           static_cast<uint32_t>(au_data[offset + 3]);
        offset += 4;

        // 检查长度有效性
        if (nal_len == 0 || offset + nal_len > total_size) {
            std::cerr << "[WARN] Invalid NAL length " << nal_len << " at NAL " << i << std::endl;
            return false;
        }

        // 写入 Annex B 起始码
        output_file_.write(reinterpret_cast<const char*>(ANNEXB_START_CODE), 4);

        // 写入 NAL 数据
        output_file_.write(reinterpret_cast<const char*>(au_data.data() + offset), nal_len);

        bytes_written_ += 4 + nal_len;
        offset += nal_len;
    }

    // 检查是否恰好消费完所有数据
    if (offset != header.au_size) {
        std::cerr << "[WARN] AU parse mismatch: expected " << header.au_size
                  << " bytes, consumed " << offset << std::endl;
        return false;
    }

    // 刷新文件
    output_file_.flush();

    return true;
}

} // namespace udp_video