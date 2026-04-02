/**
 * @file annexb_writer.hpp
 * @brief Annex B 格式恢复与文件输出
 *
 * 功能：
 * - 解析 AU 序列化格式：[4B nalu_len_be][nalu_bytes] × N
 * - 为每个 NAL 前补 0x00 00 00 01 起始码
 * - 写入 H.265 文件
 */

#ifndef ANNEXB_WRITER_HPP
#define ANNEXB_WRITER_HPP

#include "packet_header.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <fstream>

namespace udp_video {

/**
 * @brief Annex B 写入器
 */
class AnnexBWriter {
public:
    /**
     * @brief 构造函数
     * @param output_dir 输出目录
     * @param channel_id 通道 ID
     */
    AnnexBWriter(const std::string& output_dir, uint8_t channel_id);

    ~AnnexBWriter();

    /**
     * @brief 处理完整的 AU 数据
     * @param au_data AU 序列化数据
     * @param header 协议头
     * @return 成功返回 true
     */
    bool process_au(const std::vector<uint8_t>& au_data, const PacketHeader& header);

    /**
     * @brief 检查 AU 是否可解析（不写入文件）
     * @param au_data AU 序列化数据
     * @param header 协议头
     * @return 可解析返回 true
     */
    bool can_parse_au(const std::vector<uint8_t>& au_data, const PacketHeader& header);

    /**
     * @brief 将 AU 转换为 Annex B 格式（不写入文件）
     * @param au_data AU 序列化数据
     * @param header 协议头
     * @param annexb_data 输出的 Annex B 数据
     * @return 成功返回 true
     */
    bool convert_to_annexb(const std::vector<uint8_t>& au_data, const PacketHeader& header,
                           std::vector<uint8_t>& annexb_data);

    /**
     * @brief 获取写入的字节数
     */
    uint64_t get_bytes_written() const { return bytes_written_; }

    /**
     * @brief 获取写入的 AU 数
     */
    uint64_t get_au_count() const { return au_count_; }

    /**
     * @brief 获取写入的 IDR 数
     */
    uint64_t get_idr_count() const { return idr_count_; }

    /**
     * @brief 获取解析失败数
     */
    uint64_t get_parse_fail_count() const { return parse_fail_count_; }

private:
    std::string output_path_;
    std::ofstream output_file_;
    uint8_t channel_id_;

    uint64_t bytes_written_ = 0;
    uint64_t au_count_ = 0;
    uint64_t idr_count_ = 0;
    uint64_t parse_fail_count_ = 0;

    /**
     * @brief 解析 AU 并转换为 Annex B
     */
    bool parse_and_write(const std::vector<uint8_t>& au_data, const PacketHeader& header);
};

} // namespace udp_video

#endif // ANNEXB_WRITER_HPP