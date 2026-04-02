/**
 * @file crc16.cpp
 * @brief CRC-16/IBM 计算实现
 */

#include "crc16.hpp"
#include <cstring>

namespace udp_video {

// 位反转（8位）
static uint8_t reflect8(uint8_t data) {
    uint8_t result = 0;
    for (int i = 0; i < 8; i++) {
        if (data & (1 << i)) {
            result |= (1 << (7 - i));
        }
    }
    return result;
}

// 位反转（16位）
static uint16_t reflect16(uint16_t data) {
    uint16_t result = 0;
    for (int i = 0; i < 16; i++) {
        if (data & (1 << i)) {
            result |= (1 << (15 - i));
        }
    }
    return result;
}

uint16_t crc16_ibm(const uint8_t* data, size_t len) {
    uint16_t crc = 0x0000;
    uint16_t poly = 0x8005;

    for (size_t i = 0; i < len; i++) {
        // refin=true: 输入位反转
        uint8_t byte_reflected = reflect8(data[i]);
        crc ^= static_cast<uint16_t>(byte_reflected);

        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ poly;
            } else {
                crc >>= 1;
            }
        }
    }

    // refout=true: 输出位反转
    return reflect16(crc);
}

uint16_t crc16_ibm(const std::vector<uint8_t>& data) {
    return crc16_ibm(data.data(), data.size());
}

} // namespace udp_video