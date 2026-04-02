/**
 * @file crc16.hpp
 * @brief CRC-16/IBM 计算实现
 *
 * 参数: poly=0x8005, init=0x0000, refin=true, refout=true, xorout=0x0000
 */

#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstdint>
#include <cstddef>
#include <vector>

namespace udp_video {

/**
 * @brief CRC-16/IBM 计算
 * @param data 输入数据
 * @return CRC-16 校验值
 */
uint16_t crc16_ibm(const std::vector<uint8_t>& data);

/**
 * @brief CRC-16/IBM 计算（指针版本）
 * @param data 数据指针
 * @param len 数据长度
 * @return CRC-16 校验值
 */
uint16_t crc16_ibm(const uint8_t* data, size_t len);

} // namespace udp_video

#endif // CRC16_HPP