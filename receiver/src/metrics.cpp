/**
 * @file metrics.cpp
 * @brief 统计计数器实现
 */

#include "metrics.hpp"
#include <iostream>
#include <iomanip>

namespace udp_video {

void Metrics::print() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);

    std::cout << "\n[Stats] "
              << "Elapsed=" << elapsed.count() << "s "
              << "Packets=" << total_packets.load()
              << " Valid=" << valid_packets.load()
              << " CRC_fail=" << crc_fail.load()
              << " Ver_fail=" << version_fail.load()
              << " Magic_fail=" << magic_fail.load()
              << "\n        "
              << "au_size_fail=" << invalid_au_size.load()
              << " frag_total_fail=" << invalid_frag_total.load()
              << " au_timeout=" << au_timeout.load()
              << "\n        "
              << "AU_completed=" << au_completed.load()
              << " AU_parse_fail=" << au_parse_fail.load()
              << " Duplicate=" << duplicate_frag.load()
              << "\n        "
              << "FSM: wait_first_idr_drops=" << wait_first_idr_drops.load()
              << " wait_idr_drops=" << wait_idr_drops.load()
              << " entries=" << wait_idr_entries.load()
              << " recovers=" << wait_idr_recovers.load()
              << "\n        "
              << "FSM: frame_seq_gap=" << frame_seq_gap.load()
              << " invalid_idr=" << invalid_idr.load()
              << " idr_accepted=" << idr_accepted.load()
              << "\n        "
              << "Decode: frames=" << frames_decoded.load()
              << " errors=" << decode_errors.load()
              << " queue=" << decode_queue_size.load()
              << " bytes=" << bytes_pushed_decode.load()
              << "\n        "
              << "Bytes_recv=" << bytes_received.load()
              << " Bytes_written=" << bytes_written.load()
              << std::endl;
}

std::string Metrics::to_string() const {
    return std::string();
}

} // namespace udp_video