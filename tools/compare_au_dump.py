#!/usr/bin/env python3
"""
AU Dump 比较工具 - 协议层一致性排查

功能：
1. 比较发送端和接收端的 AU dump 数据
2. 按 frame_seq 对齐
3. 检查元信息和 payload hash
4. 格式归一化检查（从 .h265 逆向解析）

使用：
    python3 tools/compare_au_dump.py \
        --sender-dir ./au_dump_tx \
        --receiver-dir ./au_dump_rx \
        --h265-file ./dump/channel0.h265
"""

import argparse
import os
import json
import hashlib
import struct
from pathlib import Path
from typing import Dict, List, Optional, Tuple, NamedTuple
from dataclasses import dataclass


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 数据结构
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

@dataclass
class AuMeta:
    """AU 元信息"""
    frame_seq: int
    is_idr: bool
    primary_nal_type: int
    au_nal_count: int
    au_size: int
    frag_total: int
    pts: int
    sha256: str
    filename: str


@dataclass
class CompareResult:
    """比较结果"""
    frame_seq: int
    sender_exists: bool
    receiver_exists: bool
    meta_match: bool  # 元信息是否一致
    payload_match: bool  # payload hash 是否一致
    sender_meta: Optional[AuMeta]
    receiver_meta: Optional[AuMeta]
    sender_size: int
    receiver_size: int
    sender_hash: str
    receiver_hash: str

    def summary(self) -> str:
        """生成简要描述"""
        if not self.sender_exists and not self.receiver_exists:
            return "MISSING_BOTH"
        elif not self.sender_exists:
            return "MISSING_TX"
        elif not self.receiver_exists:
            return "MISSING_RX"
        elif self.meta_match and self.payload_match:
            return "MATCH"
        elif self.meta_match and not self.payload_match:
            return "META_MATCH_PAYLOAD_DIFF"
        elif not self.meta_match and self.payload_match:
            return "META_DIFF_PAYLOAD_MATCH"
        else:
            return "DIFF"


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# AU Manifest 解析
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def load_manifest(manifest_path: str) -> Dict[int, AuMeta]:
    """加载 AU manifest 文件"""
    if not os.path.exists(manifest_path):
        return {}

    with open(manifest_path, 'r') as f:
        data = json.load(f)

    manifest = {}
    for item in data:
        meta = AuMeta(
            frame_seq=item['frame_seq'],
            is_idr=item['is_idr'],
            primary_nal_type=item['primary_nal_type'],
            au_nal_count=item['au_nal_count'],
            au_size=item['au_size'],
            frag_total=item['frag_total'],
            pts=item['pts'],
            sha256=item['sha256'],
            filename=item['filename']
        )
        manifest[meta.frame_seq] = meta

    return manifest


def compute_file_hash(filepath: str) -> Tuple[str, int]:
    """计算文件 SHA256 hash 和大小"""
    if not os.path.exists(filepath):
        return "", 0

    sha256 = hashlib.sha256()
    size = 0
    with open(filepath, 'rb') as f:
        while chunk := f.read(8192):
            sha256.update(chunk)
            size += len(chunk)

    return sha256.hexdigest(), size


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# AU Payload 比较
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def compare_au_payloads(sender_dir: str, receiver_dir: str) -> List[CompareResult]:
    """比较发送端和接收端的 AU payload"""

    # 加载 manifest
    sender_manifest = load_manifest(os.path.join(sender_dir, 'au_manifest.json'))
    receiver_manifest = load_manifest(os.path.join(receiver_dir, 'au_manifest.json'))

    # 获取所有 frame_seq
    all_frame_seqs = set(sender_manifest.keys()) | set(receiver_manifest.keys())

    results = []
    for frame_seq in sorted(all_frame_seqs):
        sender_meta = sender_manifest.get(frame_seq)
        receiver_meta = receiver_manifest.get(frame_seq)

        sender_exists = sender_meta is not None
        receiver_exists = receiver_meta is not None

        # 计算实际文件 hash（验证 manifest）
        sender_hash, sender_size = compute_file_hash(
            os.path.join(sender_dir, sender_meta.filename) if sender_meta else ""
        )
        receiver_hash, receiver_size = compute_file_hash(
            os.path.join(receiver_dir, receiver_meta.filename) if receiver_meta else ""
        )

        # 元信息比较
        meta_match = False
        if sender_meta and receiver_meta:
            meta_match = (
                sender_meta.au_nal_count == receiver_meta.au_nal_count and
                sender_meta.au_size == receiver_meta.au_size and
                sender_meta.primary_nal_type == receiver_meta.primary_nal_type and
                sender_meta.is_idr == receiver_meta.is_idr and
                sender_meta.pts == receiver_meta.pts
            )

        # payload hash 比较
        payload_match = sender_hash == receiver_hash

        result = CompareResult(
            frame_seq=frame_seq,
            sender_exists=sender_exists,
            receiver_exists=receiver_exists,
            meta_match=meta_match,
            payload_match=payload_match,
            sender_meta=sender_meta,
            receiver_meta=receiver_meta,
            sender_size=sender_size,
            receiver_size=receiver_size,
            sender_hash=sender_hash,
            receiver_hash=receiver_hash
        )
        results.append(result)

    return results


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Annex B 逆向解析（格式归一化检查）
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def find_start_code(data: bytes, offset: int) -> Optional[Tuple[int, int]]:
    """
    从指定偏移开始向后搜索起始码位置
    返回 (起始码后的第一个字节位置, 起始码长度)
    """
    while offset < len(data) - 2:
        if offset + 4 <= len(data):
            if data[offset:offset+4] == b'\x00\x00\x00\x01':
                return offset + 4, 4
        if offset + 3 <= len(data):
            if data[offset:offset+3] == b'\x00\x00\x01':
                return offset + 3, 3
        offset += 1
    return None, 0


def parse_h265_to_nals(h265_data: bytes) -> List[bytes]:
    """
    解析 Annex B H.265 字节流，提取 NAL 数据（不含起始码）
    """
    nals = []
    offset = 0

    while offset < len(h265_data):
        nal_start, sc_len = find_start_code(h265_data, offset)

        if nal_start is None:
            break

        # 查找下一个起始码
        next_start, _ = find_start_code(h265_data, nal_start + 1)

        if next_start is None:
            nal_data = h265_data[nal_start:]
            next_offset = len(h265_data)
        else:
            # 确定起始码长度
            if next_start >= 4 and h265_data[next_start-4:next_start] == b'\x00\x00\x00\x01':
                nal_data = h265_data[nal_start:next_start-4]
            elif next_start >= 3 and h265_data[next_start-3:next_start] == b'\x00\x00\x01':
                nal_data = h265_data[nal_start:next_start-3]
            else:
                nal_data = h265_data[nal_start:next_start]
            next_offset = next_start

        if len(nal_data) > 0:
            nals.append(nal_data)

        offset = next_offset

    return nals


def reconstruct_au_from_nals(nals: List[bytes]) -> bytes:
    """
    从 NAL 列表重建 AU payload 格式
    AU_payload = [4B nalu_len_be][nalu_bytes] × au_nal_count
    """
    payload = b''
    for nal in nals:
        payload += struct.pack('>I', len(nal))
        payload += nal
    return payload


def h265_to_au_format(h265_file: str) -> List[Tuple[int, bytes]]:
    """
    从 .h265 文件逆向解析为 AU 格式
    返回 [(frame_seq_estimate, au_payload)] 列表

    注意：frame_seq 只是估算（按 IDR 分组），无法准确对应原始 frame_seq
    """
    if not os.path.exists(h265_file):
        return []

    with open(h265_file, 'rb') as f:
        h265_data = f.read()

    # 解析所有 NAL
    nals = parse_h265_to_nals(h265_data)

    # 按 IDR 分组构建 AU
    aus = []
    current_nals = []
    frame_seq_estimate = 0

    for nal in nals:
        nal_type = (nal[0] >> 1) & 0x3F

        # IDR NAL types: 19, 20, 21
        is_idr = nal_type in (19, 20, 21)

        if is_idr and current_nals:
            # 当前 NAL 列表构成一个 AU
            au_payload = reconstruct_au_from_nals(current_nals)
            aus.append((frame_seq_estimate, au_payload))
            frame_seq_estimate += 1
            current_nals = []

        current_nals.append(nal)

    # 最后一个 AU
    if current_nals:
        au_payload = reconstruct_au_from_nals(current_nals)
        aus.append((frame_seq_estimate, au_payload))

    return aus


def check_h265_vs_dump(h265_file: str, receiver_dir: str) -> List[Dict]:
    """
    检查 .h265 文件与 receiver AU dump 的格式一致性
    """
    results = []

    # 从 .h265 逆向解析
    h265_aus = h265_to_au_format(h265_file)

    # 加载 receiver manifest
    receiver_manifest = load_manifest(os.path.join(receiver_dir, 'au_manifest.json'))

    # 按 AU 数量对齐（注意：frame_seq 无法精确对应）
    for i, (_, h265_payload) in enumerate(h265_aus):
        # 查找对应 frame_seq（假设顺序一致）
        # 这里只能做数量检查，无法精确匹配

        h265_hash = hashlib.sha256(h265_payload).hexdigest()

        result = {
            'au_index': i,
            'h265_size': len(h265_payload),
            'h265_hash': h265_hash,
            'note': '无法精确对应 frame_seq，仅做数量检查'
        }

        # 如果有匹配的 frame_seq，比较 payload
        if i in receiver_manifest:
            rx_meta = receiver_manifest[i]
            rx_hash, _ = compute_file_hash(os.path.join(receiver_dir, rx_meta.filename))
            result['rx_size'] = rx_meta.au_size
            result['rx_hash'] = rx_hash
            result['match'] = h265_hash == rx_hash
        else:
            result['rx_size'] = 'N/A'
            result['rx_hash'] = 'N/A'
            result['match'] = False

        results.append(result)

    return results


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 报告生成
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def generate_report(results: List[CompareResult], sender_dir: str, receiver_dir: str) -> str:
    """生成比较报告"""

    report_lines = []
    report_lines.append("=" * 80)
    report_lines.append("AU Dump 比较报告 - 协议层一致性排查")
    report_lines.append("=" * 80)
    report_lines.append(f"发送端目录: {sender_dir}")
    report_lines.append(f"接收端目录: {receiver_dir}")
    report_lines.append(f"比较数量: {len(results)} AU")
    report_lines.append("")

    # 统计
    match_count = sum(1 for r in results if r.summary() == "MATCH")
    missing_tx = sum(1 for r in results if r.summary() == "MISSING_TX")
    missing_rx = sum(1 for r in results if r.summary() == "MISSING_RX")
    payload_diff = sum(1 for r in results if r.summary() == "META_MATCH_PAYLOAD_DIFF")
    meta_diff = sum(1 for r in results if r.summary() in ("META_DIFF_PAYLOAD_MATCH", "DIFF"))

    report_lines.append("统计摘要:")
    report_lines.append(f"  完全一致 (MATCH): {match_count}")
    report_lines.append(f"  发送端缺失 (MISSING_TX): {missing_tx}")
    report_lines.append(f"  接收端缺失 (MISSING_RX): {missing_rx}")
    report_lines.append(f"  Payload 不一致 (META_MATCH_PAYLOAD_DIFF): {payload_diff}")
    report_lines.append(f"  元信息不一致: {meta_diff}")
    report_lines.append("")

    # 详细结果（仅显示不一致项）
    report_lines.append("不一致详情:")
    for r in results:
        if r.summary() != "MATCH":
            report_lines.append(f"  frame_seq={r.frame_seq}: {r.summary()}")
            if r.sender_exists and r.receiver_exists:
                report_lines.append(f"    TX: size={r.sender_size} hash={r.sender_hash[:16]}...")
                report_lines.append(f"    RX: size={r.receiver_size} hash={r.receiver_hash[:16]}...")
                if r.sender_meta and r.receiver_meta:
                    report_lines.append(f"    TX meta: nal_count={r.sender_meta.au_nal_count} "
                                        f"nal_type={r.sender_meta.primary_nal_type} "
                                        f"is_idr={r.sender_meta.is_idr}")
                    report_lines.append(f"    RX meta: nal_count={r.receiver_meta.au_nal_count} "
                                        f"nal_type={r.receiver_meta.primary_nal_type} "
                                        f"is_idr={r.receiver_meta.is_idr}")

    report_lines.append("")
    report_lines.append("=" * 80)

    # 结论
    report_lines.append("结论:")
    if match_count == len(results):
        report_lines.append("  ✅ 所有 AU 完全一致，协议层传输正确")
    elif missing_rx > 0 and payload_diff == 0:
        report_lines.append(f"  ⚠️ 接收端缺失 {missing_rx} 个 AU，可能是 WAIT_FIRST_IDR/WAIT_IDR 丢弃")
        report_lines.append("  其他 AU payload 一致，协议层传输正确")
    elif payload_diff > 0:
        report_lines.append(f"  ❌ {payload_diff} 个 AU payload 不一致，存在数据损坏")
    else:
        report_lines.append("  ⚠️ 存在多种差异，需要进一步排查")

    report_lines.append("")
    report_lines.append("=" * 80)

    return "\n".join(report_lines)


# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 主程序
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def main():
    parser = argparse.ArgumentParser(description='AU Dump 比较工具')
    parser.add_argument('--sender-dir', required=True, help='发送端 AU dump 目录')
    parser.add_argument('--receiver-dir', required=True, help='接收端 AU dump 目录')
    parser.add_argument('--h265-file', default=None, help='接收端 .h265 文件（格式归一化检查）')
    parser.add_argument('--output', default=None, help='报告输出文件')
    parser.add_argument('--verbose', action='store_true', help='显示所有比较结果')

    args = parser.parse_args()

    # 比较 AU dump
    results = compare_au_payloads(args.sender_dir, args.receiver_dir)

    # 生成报告
    report = generate_report(results, args.sender_dir, args.receiver_dir)

    # 格式归一化检查（可选）
    if args.h265_file:
        report += "\n\n格式归一化检查:\n"
        report += f"  .h265 文件: {args.h265_file}\n"

        h265_results = check_h265_vs_dump(args.h265_file, args.receiver_dir)

        match_count = sum(1 for r in h265_results if r['match'])
        report += f"  .h265 解析 AU 数: {len(h265_results)}\n"
        report += f"  与 receiver dump 匹配数: {match_count}\n"

        if match_count == len(h265_results):
            report += "  ✅ Annex B 恢复正确，格式归一化一致\n"
        else:
            report += "  ⚠️ 存在不一致，可能是 Annex B 起始码恢复差异\n"

    # 输出
    print(report)

    if args.output:
        with open(args.output, 'w') as f:
            f.write(report)
        print(f"报告已保存: {args.output}")


if __name__ == '__main__':
    main()