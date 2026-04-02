/**
 * @file crc16.cpp
 * @brief CRC-16/IBM 实现
 */

#include "crc16.hpp"

namespace udp_video {

uint16_t crc16_ibm(const uint8_t* data, size_t len) {
    uint16_t crc = 0x0000;
    uint16_t poly = 0x8005;

    for (size_t i = 0; i < len; ++i) {
        // refin=true: 输入字节按位反转
        uint8_t byte = data[i];
        uint8_t reflected_byte = 0;
        for (int b = 0; b < 8; ++b) {
            reflected_byte |= ((byte >> b) & 1) << (7 - b);
        }

        crc ^= static_cast<uint16_t>(reflected_byte);

        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
    }

    // refout=true: 输出按位反转（16位）
    uint16_t reflected_crc = 0;
    for (int b = 0; b < 16; ++b) {
        reflected_crc |= ((crc >> b) & 1) << (15 - b);
    }

    // xorout=0x0000: 无异或输出
    return reflected_crc;
}

uint16_t crc16_ibm(const std::vector<uint8_t>& data) {
    return crc16_ibm(data.data(), data.size());
}

} // namespace udp_video