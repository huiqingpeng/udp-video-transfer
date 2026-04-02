/**
 * @file crc16.hpp
 * @brief CRC-16/IBM 实现
 *
 * 协议定义的 CRC 参数：
 * - poly   = 0x8005
 * - init   = 0x0000
 * - refin  = true
 * - refout = true
 * - xorout = 0x0000
 */

#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstdint>
#include <vector>
#include <cstddef>

namespace udp_video {

/**
 * @brief CRC-16/IBM 计算
 * @param data 输入数据
 * @param len 数据长度
 * @return CRC 校验值
 *
 * 注意：协议要求 refin=true, refout=true
 * 即输入字节和输出结果都需要按位反转
 */
uint16_t crc16_ibm(const uint8_t* data, size_t len);

/**
 * @brief CRC-16/IBM 计算 (vector 版本)
 */
uint16_t crc16_ibm(const std::vector<uint8_t>& data);

} // namespace udp_video

#endif // CRC16_HPP