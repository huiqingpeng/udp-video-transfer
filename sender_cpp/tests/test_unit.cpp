/**
 * @file test_unit.cpp
 * @brief 单元测试：CRC、NAL 解析、协议头序列化
 */

#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cassert>

#include "crc16.hpp"
#include "h265_nal.hpp"
#include "protocol.hpp"

using namespace udp_video;

// 测试计数
int tests_passed = 0;
int tests_failed = 0;

#define TEST(name) void test_##name()
#define RUN_TEST(name) do { \
    std::cout << "Running: " << #name << "... "; \
    try { \
        test_##name(); \
        std::cout << "\033[32mPASS\033[0m" << std::endl; \
        tests_passed++; \
    } catch (const std::exception& e) { \
        std::cout << "\033[31mFAIL\033[0m: " << e.what() << std::endl; \
        tests_failed++; \
    } \
} while(0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        throw std::runtime_error("Assertion failed: " #a " != " #b); \
    } \
} while(0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        throw std::runtime_error("Assertion failed: " #x); \
    } \
} while(0)

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// CRC-16 测试
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST(crc16_basic) {
    // 测试字符串 "123456789" 的 CRC
    std::vector<uint8_t> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    uint16_t crc = crc16_ibm(data);

    // CRC-16/IBM 对于 "123456789" 的期望值是 0xA47B
    ASSERT_EQ(crc, 0xA47B);
}

TEST(crc16_empty) {
    std::vector<uint8_t> empty;
    uint16_t crc = crc16_ibm(empty);
    ASSERT_EQ(crc, 0x0000);
}

TEST(crc16_zeros) {
    std::vector<uint8_t> zeros(10, 0);
    uint16_t crc = crc16_ibm(zeros);
    // CRC-16/IBM 对全零数据返回 0x0000
    ASSERT_EQ(crc, 0x0000);
}

TEST(crc16_consistency) {
    std::vector<uint8_t> data = {0xAA, 0x55, 0x02, 0x00, 0x00, 0x00, 0x01};

    // 两次计算应该得到相同结果
    uint16_t crc1 = crc16_ibm(data);
    uint16_t crc2 = crc16_ibm(data.data(), data.size());
    ASSERT_EQ(crc1, crc2);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// NAL 解析测试
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST(nal_find_start_code_4byte) {
    // 4 字节起始码
    std::vector<uint8_t> data = {0x00, 0x00, 0x00, 0x01, 0x40, 0x01};

    size_t nal_start = 0;
    uint8_t sc_len = 0;

    bool found = find_start_code(data, 0, nal_start, sc_len);
    ASSERT_TRUE(found);
    ASSERT_EQ(sc_len, 4);
    ASSERT_EQ(nal_start, 4);
}

TEST(nal_find_start_code_3byte) {
    // 3 字节起始码
    std::vector<uint8_t> data = {0x00, 0x00, 0x01, 0x28, 0x01};

    size_t nal_start = 0;
    uint8_t sc_len = 0;

    bool found = find_start_code(data, 0, nal_start, sc_len);
    ASSERT_TRUE(found);
    ASSERT_EQ(sc_len, 3);
    ASSERT_EQ(nal_start, 3);
}

TEST(nal_find_start_code_none) {
    std::vector<uint8_t> data = {0xFF, 0xFF, 0xFF, 0xFF};

    size_t nal_start = 0;
    uint8_t sc_len = 0;

    bool found = find_start_code(data, 0, nal_start, sc_len);
    ASSERT_TRUE(!found);
}

TEST(nal_parse_single) {
    // 单个 NAL：VPS (type 32)
    std::vector<uint8_t> data = {
        0x00, 0x00, 0x00, 0x01,  // 起始码
        0x40, 0x01,             // NAL header (VPS)
        0x0C, 0x01, 0xFF, 0xFF  // NAL payload
    };

    size_t parsed_len = 0;
    std::vector<NalUnit> nals = parse_nal_units(data, parsed_len);

    // 没有下一个起始码，所以不应该返回这个 NAL（不完整）
    ASSERT_EQ(nals.size(), 0);
}

TEST(nal_parse_multiple) {
    // 两个 NAL
    std::vector<uint8_t> data = {
        0x00, 0x00, 0x00, 0x01,  // 起始码 1
        0x40, 0x01,              // VPS header
        0x0C, 0x01,              // VPS payload
        0x00, 0x00, 0x00, 0x01,  // 起始码 2
        0x42, 0x01,              // SPS header
        0x01, 0x02               // SPS payload
    };

    size_t parsed_len = 0;
    std::vector<NalUnit> nals = parse_nal_units(data, parsed_len);

    // 应该返回第一个 NAL（VPS），因为它后面有起始码
    ASSERT_EQ(nals.size(), 1);
    ASSERT_EQ(nals[0].nal_type, 32);  // VPS
    ASSERT_EQ(nals[0].start_code_len, 4);
}

TEST(nal_type_name) {
    ASSERT_EQ(get_nal_type_name(32), "VPS");
    ASSERT_EQ(get_nal_type_name(33), "SPS");
    ASSERT_EQ(get_nal_type_name(34), "PPS");
    ASSERT_EQ(get_nal_type_name(35), "AUD");
    ASSERT_EQ(get_nal_type_name(19), "IDR_W_RADL");
    ASSERT_EQ(get_nal_type_name(20), "IDR_N_LP");
    ASSERT_EQ(get_nal_type_name(21), "CRA_NUT");
    ASSERT_EQ(get_nal_type_name(1), "TRAIL_N");
    ASSERT_EQ(get_nal_type_name(10), "TRAIL_R");
}

TEST(nal_is_vcl) {
    ASSERT_TRUE(is_vcl_nal_type(0));
    ASSERT_TRUE(is_vcl_nal_type(19));
    ASSERT_TRUE(is_vcl_nal_type(21));
    ASSERT_TRUE(!is_vcl_nal_type(32));
    ASSERT_TRUE(!is_vcl_nal_type(35));
}

TEST(nal_is_idr) {
    ASSERT_TRUE(is_idr_nal_type(19));
    ASSERT_TRUE(is_idr_nal_type(20));
    ASSERT_TRUE(is_idr_nal_type(21));
    ASSERT_TRUE(!is_idr_nal_type(1));
    ASSERT_TRUE(!is_idr_nal_type(32));
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 协议头测试
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST(protocol_header_size) {
    ProtocolHeader header;
    std::vector<uint8_t> bytes = serialize_header(header);
    ASSERT_EQ(bytes.size(), HEADER_SIZE);
}

TEST(protocol_header_magic) {
    ProtocolHeader header;
    header.magic = PROTOCOL_MAGIC;
    header.version = PROTOCOL_VERSION;

    std::vector<uint8_t> bytes = serialize_header(header);

    // 检查 magic (大端)
    ASSERT_EQ(bytes[0], 0xAA);
    ASSERT_EQ(bytes[1], 0x55);
}

TEST(protocol_header_version) {
    ProtocolHeader header;
    header.version = 0x02;

    std::vector<uint8_t> bytes = serialize_header(header);
    ASSERT_EQ(bytes[2], 0x02);
}

TEST(protocol_header_channel) {
    ProtocolHeader header;
    header.channel_id = 3;

    std::vector<uint8_t> bytes = serialize_header(header);
    ASSERT_EQ(bytes[3], 3);
}

TEST(protocol_header_frame_seq) {
    ProtocolHeader header;
    header.frame_seq = 0x12345678;

    std::vector<uint8_t> bytes = serialize_header(header);

    // 大端序
    ASSERT_EQ(bytes[4], 0x12);
    ASSERT_EQ(bytes[5], 0x34);
    ASSERT_EQ(bytes[6], 0x56);
    ASSERT_EQ(bytes[7], 0x78);
}

TEST(protocol_header_pts) {
    ProtocolHeader header;
    header.pts = 0x0102030405060708ULL;

    std::vector<uint8_t> bytes = serialize_header(header);

    // 大端序 (8 字节)
    ASSERT_EQ(bytes[16], 0x01);
    ASSERT_EQ(bytes[17], 0x02);
    ASSERT_EQ(bytes[18], 0x03);
    ASSERT_EQ(bytes[19], 0x04);
    ASSERT_EQ(bytes[20], 0x05);
    ASSERT_EQ(bytes[21], 0x06);
    ASSERT_EQ(bytes[22], 0x07);
    ASSERT_EQ(bytes[23], 0x08);
}

TEST(protocol_header_crc) {
    ProtocolHeader header;
    header.magic = 0xAA55;
    header.version = 0x02;
    header.channel_id = 0;
    header.frame_seq = 0;
    header.frag_idx = 0;
    header.frag_total = 1;
    header.is_idr = 1;
    header.primary_nal_type = 20;
    header.au_nal_count = 5;
    header.pts = 0;
    header.au_size = 1000;

    std::vector<uint8_t> bytes = serialize_header(header);

    // 手动计算 CRC 并验证
    uint16_t computed_crc = crc16_ibm(bytes.data(), 28);
    uint16_t header_crc = (bytes[28] << 8) | bytes[29];

    ASSERT_EQ(computed_crc, header_crc);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 分片测试
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST(fragment_single) {
    // 小于 1440 字节，单分片
    std::vector<uint8_t> payload(1000, 0xAB);

    ProtocolHeader header;
    header.channel_id = 0;
    header.frame_seq = 1;
    header.is_idr = 1;
    header.primary_nal_type = 20;
    header.au_nal_count = 5;
    header.pts = 3600;

    std::vector<std::vector<uint8_t>> fragments = build_fragments(payload, header);

    ASSERT_EQ(fragments.size(), 1);
    ASSERT_EQ(fragments[0].size(), HEADER_SIZE + 1000);
}

TEST(fragment_multiple) {
    // 大于 1440 字节，多分片
    std::vector<uint8_t> payload(3000, 0xAB);

    ProtocolHeader header;
    header.frame_seq = 1;

    std::vector<std::vector<uint8_t>> fragments = build_fragments(payload, header);

    // 3000 / 1440 = 2.08 → 3 分片
    ASSERT_EQ(fragments.size(), 3);

    // 检查分片大小
    ASSERT_EQ(fragments[0].size(), HEADER_SIZE + 1440);
    ASSERT_EQ(fragments[1].size(), HEADER_SIZE + 1440);
    ASSERT_EQ(fragments[2].size(), HEADER_SIZE + 120);  // 3000 - 2880 = 120
}

TEST(fragment_total_size) {
    // 验证所有分片 payload 总和等于原始大小
    std::vector<uint8_t> payload(5000, 0xCD);

    ProtocolHeader header;
    header.frame_seq = 1;

    std::vector<std::vector<uint8_t>> fragments = build_fragments(payload, header);

    size_t total_payload = 0;
    for (const auto& frag : fragments) {
        total_payload += frag.size() - HEADER_SIZE;
    }

    ASSERT_EQ(total_payload, 5000);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// AU 序列化测试
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST(au_serialize_single_nal) {
    AccessUnit au;
    au.pts = 3600;
    au.is_idr = true;
    au.primary_nal_type = 20;

    NalUnit nal;
    nal.nal_type = 20;  // IDR_N_LP
    nal.data = {0x28, 0x01, 0x02, 0x03};
    nal.start_code_len = 4;

    au.nals.push_back(nal);

    std::vector<uint8_t> payload = serialize_au(au);

    // 格式: [4B length][NAL data]
    ASSERT_EQ(payload.size(), 4 + 4);  // 4 bytes length + 4 bytes NAL
    ASSERT_EQ(payload[0], 0x00);
    ASSERT_EQ(payload[1], 0x00);
    ASSERT_EQ(payload[2], 0x00);
    ASSERT_EQ(payload[3], 0x04);  // length = 4
}

TEST(au_serialize_multiple_nals) {
    AccessUnit au;

    // VPS
    NalUnit vps;
    vps.nal_type = 32;
    vps.data = {0x40, 0x01};
    au.nals.push_back(vps);

    // SPS
    NalUnit sps;
    sps.nal_type = 33;
    sps.data = {0x42, 0x01, 0x01};
    au.nals.push_back(sps);

    std::vector<uint8_t> payload = serialize_au(au);

    // 4 + 2 + 4 + 3 = 13 bytes
    ASSERT_EQ(payload.size(), 13);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// 端口映射测试
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

TEST(port_mapping) {
    ASSERT_EQ(CHANNEL_PORTS[0], 5000);
    ASSERT_EQ(CHANNEL_PORTS[1], 5001);
    ASSERT_EQ(CHANNEL_PORTS[2], 5002);
    ASSERT_EQ(CHANNEL_PORTS[3], 5003);
}

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
// Main
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

int main() {
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "UDP Video Sender Unit Tests" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    // CRC 测试
    std::cout << "\n--- CRC-16 Tests ---" << std::endl;
    RUN_TEST(crc16_basic);
    RUN_TEST(crc16_empty);
    RUN_TEST(crc16_zeros);
    RUN_TEST(crc16_consistency);

    // NAL 解析测试
    std::cout << "\n--- NAL Parsing Tests ---" << std::endl;
    RUN_TEST(nal_find_start_code_4byte);
    RUN_TEST(nal_find_start_code_3byte);
    RUN_TEST(nal_find_start_code_none);
    RUN_TEST(nal_parse_single);
    RUN_TEST(nal_parse_multiple);
    RUN_TEST(nal_type_name);
    RUN_TEST(nal_is_vcl);
    RUN_TEST(nal_is_idr);

    // 协议头测试
    std::cout << "\n--- Protocol Header Tests ---" << std::endl;
    RUN_TEST(protocol_header_size);
    RUN_TEST(protocol_header_magic);
    RUN_TEST(protocol_header_version);
    RUN_TEST(protocol_header_channel);
    RUN_TEST(protocol_header_frame_seq);
    RUN_TEST(protocol_header_pts);
    RUN_TEST(protocol_header_crc);

    // 分片测试
    std::cout << "\n--- Fragment Tests ---" << std::endl;
    RUN_TEST(fragment_single);
    RUN_TEST(fragment_multiple);
    RUN_TEST(fragment_total_size);

    // AU 序列化测试
    std::cout << "\n--- AU Serialization Tests ---" << std::endl;
    RUN_TEST(au_serialize_single_nal);
    RUN_TEST(au_serialize_multiple_nals);

    // 端口映射测试
    std::cout << "\n--- Port Mapping Tests ---" << std::endl;
    RUN_TEST(port_mapping);

    // 总结
    std::cout << "\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "Test Summary" << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << "Passed: " << tests_passed << std::endl;
    std::cout << "Failed: " << tests_failed << std::endl;
    std::cout << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;

    return tests_failed > 0 ? 1 : 0;
}