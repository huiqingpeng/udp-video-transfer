#!/usr/bin/env python3
"""
UDP Video Sender - 自定义 UDP 视频传输协议 v1.1 发送端

根据 docs/custom_udp_video_protocol_v1.1.md 实现：
- 协议头 30 字节，全部大端序
- CRC-16/IBM 校验
- AU 序列化与分片
- Annex B 解析与 AU 组帧
- AU dump 调试功能（协议层一致性排查）
"""

import argparse
import logging
import os
import socket
import subprocess
import struct
import sys
import time
import json
import hashlib
from dataclasses import dataclass, field, asdict
from typing import List, Optional, Tuple, Dict
from enum import IntEnum
import threading

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 协议常量定义
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

PROTOCOL_MAGIC = 0xAA55
PROTOCOL_VERSION = 0x02
HEADER_SIZE = 30
FRAGMENT_PAYLOAD_SIZE = 1440  # 单包 AU 数据净载荷上限

# H.265 NAL Unit Types
class H265NalType(IntEnum):
    VPS = 32
    SPS = 33
    PPS = 34
    AUD = 35
    SEI_PREFIX = 39
    SEI_SUFFIX = 40
    IDR_W_RADL = 19
    IDR_N_LP = 20
    CRA_NUT = 21

# 端口映射
CHANNEL_PORT_MAP = {0: 5000, 1: 5001, 2: 5002, 3: 5003}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# CRC-16/IBM 实现
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def crc16_ibm(data: bytes) -> int:
    """
    CRC-16/IBM 计算
    参数: poly=0x8005, init=0x0000, refin=true, refout=true, xorout=0x0000
    """
    crc = 0x0000
    poly = 0x8005

    for byte in data:
        # refin=true: 输入位反转
        byte_reflected = int('{:08b}'.format(byte)[::-1], 2)
        crc ^= byte_reflected

        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ poly
            else:
                crc >>= 1

    # refout=true: 输出位反转（16位）
    crc_reflected = int('{:016b}'.format(crc)[::-1], 2)
    # xorout=0x0000: 无异或输出
    return crc_reflected

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Annex B 解析
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

@dataclass
class NalUnit:
    """NAL Unit 结构"""
    nal_type: int
    data: bytes  # 不含起始码
    start_code_len: int  # 起始码长度 (3 或 4)

def find_start_code(data: bytes, offset: int) -> Optional[int]:
    """
    从指定偏移开始向后搜索起始码位置
    返回起始码后的第一个字节位置（即 NAL 数据开始位置）
    同时返回起始码长度
    """
    while offset < len(data) - 2:
        # 先尝试匹配 4 字节起始码
        if offset + 4 <= len(data):
            if data[offset:offset+4] == b'\x00\x00\x00\x01':
                return offset + 4, 4
        # 再尝试匹配 3 字节起始码
        if offset + 3 <= len(data):
            if data[offset:offset+3] == b'\x00\x00\x01':
                return offset + 3, 3
        offset += 1
    return None, 0

def parse_nal_units(stream: bytes) -> List[NalUnit]:
    """
    解析 Annex B 字节流，提取所有 NAL Unit
    返回 NAL 列表，每个 NAL 不含起始码

    注意：只返回完整的 NAL（后面有起始码或到达流末尾）
    不完整的 NAL（在 buffer 末尾被截断）不会被返回
    """
    nals = []
    offset = 0

    while offset < len(stream):
        # 查找起始码
        # 返回值: (nal_start, sc_len) - nal_start 是 NAL 数据的开始位置（起始码之后）
        nal_start, sc_len = find_start_code(stream, offset)

        if nal_start is None:
            # 未找到起始码，跳过剩余数据
            break

        # 查找下一个起始码（从当前 NAL 数据之后开始查找）
        # 注意：需要跳过至少 1 字节，避免找到当前起始码的尾部
        next_nal_start, next_sc_len = find_start_code(stream, nal_start + 1)

        if next_nal_start is None:
            # 没有找到下一个起始码，这个 NAL 可能不完整
            # 不返回它，让调用者读取更多数据
            break

        # 找到下一个起始码，当前 NAL 是完整的
        # next_nal_start 是下一个 NAL 数据的开始位置（下一个起始码之后）
        # next_sc_len 是下一个起始码的长度
        # 所以下一个起始码的开始位置 = next_nal_start - next_sc_len
        # 当前 NAL 数据结束于下一个起始码的开始位置
        nal_end = next_nal_start - next_sc_len
        nal_data = stream[nal_start:nal_end]
        # next_offset 设为下一个起始码的开始位置
        next_offset = nal_end

        if len(nal_data) > 0:
            # 解析 NAL type: (nal[0] >> 1) & 0x3F
            nal_type = (nal_data[0] >> 1) & 0x3F
            nals.append(NalUnit(
                nal_type=nal_type,
                data=nal_data,
                start_code_len=sc_len
            ))

        offset = next_offset

    return nals

def get_nal_type_name(nal_type: int) -> str:
    """获取 NAL 类型名称"""
    names = {
        32: "VPS", 33: "SPS", 34: "PPS", 35: "AUD",
        39: "SEI_PREFIX", 40: "SEI_SUFFIX",
        19: "IDR_W_RADL", 20: "IDR_N_LP", 21: "CRA_NUT",
    }
    # TRAIL_N: 0-8, TRAIL_R: 9-18
    if 0 <= nal_type <= 8:
        return "TRAIL_N"
    elif 9 <= nal_type <= 18:
        return "TRAIL_R"
    elif 22 <= nal_type <= 31:
        return "RSV_IRAP_VCL"
    return names.get(nal_type, f"UNKNOWN({nal_type})")

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# AU 组帧
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

@dataclass
class AccessUnit:
    """Access Unit (视频帧)"""
    nals: List[NalUnit]
    pts: int  # 90kHz 时间戳
    is_idr: bool = False
    primary_nal_type: int = 0

def build_au_from_nals(nals: List[NalUnit], pts: int,
                       cached_vps: Optional[bytes],
                       cached_sps: Optional[bytes],
                       cached_pps: Optional[bytes]) -> AccessUnit:
    """
    从 NAL 列表构建 AU
    检查是否为 IDR，必要时注入缓存的参数集

    AU NAL 顺序规范：
    1. AUD (Access Unit Delimiter) - 每个 AU 的第一个 NAL
    2. VPS/SPS/PPS (参数集) - IDR 帧需要
    3. VCL (视频编码层) - 帧数据
    4. SEI_SUFFIX (可选)
    """
    # 找到首个 VCL NAL
    first_vcl_nal = None
    first_vcl_idx = -1

    for i, nal in enumerate(nals):
        if nal.nal_type < 32:  # VCL NAL types: 0-31
            first_vcl_nal = nal
            first_vcl_idx = i
            break

    if first_vcl_nal is None:
        # 无 VCL NAL，这不是有效 AU
        return None

    # 判断是否为 IDR/CRA
    is_idr = first_vcl_nal.nal_type in (H265NalType.IDR_W_RADL, H265NalType.IDR_N_LP, H265NalType.CRA_NUT)

    # 检查参数集
    has_vps = any(nal.nal_type == H265NalType.VPS for nal in nals)
    has_sps = any(nal.nal_type == H265NalType.SPS for nal in nals)
    has_pps = any(nal.nal_type == H265NalType.PPS for nal in nals)

    # 提取 AUD NAL（如果有）
    aud_nal = None
    for nal in nals:
        if nal.nal_type == H265NalType.AUD:
            aud_nal = nal
            break

    # IDR AU 需要完整参数集，缺少时注入缓存
    inject_nals = []
    if is_idr:
        if not has_vps and cached_vps:
            inject_nals.append(NalUnit(nal_type=H265NalType.VPS, data=cached_vps, start_code_len=4))
        if not has_sps and cached_sps:
            inject_nals.append(NalUnit(nal_type=H265NalType.SPS, data=cached_sps, start_code_len=4))
        if not has_pps and cached_pps:
            inject_nals.append(NalUnit(nal_type=H265NalType.PPS, data=cached_pps, start_code_len=4))

    # 构建最终 NAL 列表（正确顺序）
    final_nals = []

    # 1. AUD 作为第一个 NAL
    if aud_nal:
        final_nals.append(aud_nal)

    # 2. VPS/SPS/PPS 参数集
    for nal in nals:
        if nal.nal_type in (H265NalType.VPS, H265NalType.SPS, H265NalType.PPS):
            final_nals.append(nal)

    # 3. 注入的参数集（如果 IDR 缺少参数集）
    final_nals.extend(inject_nals)

    # 4. VCL 和其他 NAL
    for nal in nals:
        if nal.nal_type == H265NalType.AUD:
            continue  # 已添加
        if nal.nal_type in (H265NalType.VPS, H265NalType.SPS, H265NalType.PPS):
            continue  # 已添加
        final_nals.append(nal)

    return AccessUnit(
        nals=final_nals,
        pts=pts,
        is_idr=is_idr,
        primary_nal_type=first_vcl_nal.nal_type
    )

def serialize_au(au: AccessUnit) -> bytes:
    """
    序列化 AU 为协议格式
    AU_payload = [4B nalu_len_be][nalu_bytes] × au_nal_count
    """
    payload = b''
    for nal in au.nals:
        # 4 字节大端长度 + NAL 数据（不含起始码）
        nalu_len = len(nal.data)
        payload += struct.pack('>I', nalu_len)
        payload += nal.data
    return payload

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 协议头序列化
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

@dataclass
class ProtocolHeader:
    """协议头结构"""
    magic: int = PROTOCOL_MAGIC
    version: int = PROTOCOL_VERSION
    channel_id: int = 0
    frame_seq: int = 0
    frag_idx: int = 0
    frag_total: int = 1
    is_idr: int = 0
    primary_nal_type: int = 0
    au_nal_count: int = 0
    pts: int = 0
    au_size: int = 0
    header_crc: int = 0

def serialize_header(header: ProtocolHeader) -> bytes:
    """
    序列化协议头（30 字节，全部大端序）
    """
    # 前 28 字节（不含 CRC）
    header_bytes = struct.pack(
        '>H B B I H H B B H Q I',
        header.magic,
        header.version,
        header.channel_id,
        header.frame_seq,
        header.frag_idx,
        header.frag_total,
        header.is_idr,
        header.primary_nal_type,
        header.au_nal_count,
        header.pts,
        header.au_size
    )

    # 计算 CRC（覆盖前 28 字节）
    crc = crc16_ibm(header_bytes)

    # 添加 CRC（2 字节大端）
    header_bytes += struct.pack('>H', crc)

    return header_bytes

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# UDP 发送
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def build_fragments(au_payload: bytes, header_template: ProtocolHeader) -> List[bytes]:
    """
    将 AU 按固定大小分片，构建完整 UDP 包列表
    """
    au_size = len(au_payload)
    frag_total = (au_size + FRAGMENT_PAYLOAD_SIZE - 1) // FRAGMENT_PAYLOAD_SIZE
    if frag_total == 0:
        frag_total = 1

    fragments = []
    for frag_idx in range(frag_total):
        # 计算分片偏移和长度
        offset = frag_idx * FRAGMENT_PAYLOAD_SIZE
        end_offset = min(offset + FRAGMENT_PAYLOAD_SIZE, au_size)
        frag_payload = au_payload[offset:end_offset]

        # 构建协议头
        header = ProtocolHeader(
            magic=header_template.magic,
            version=header_template.version,
            channel_id=header_template.channel_id,
            frame_seq=header_template.frame_seq,
            frag_idx=frag_idx,
            frag_total=frag_total,
            is_idr=header_template.is_idr,
            primary_nal_type=header_template.primary_nal_type,
            au_nal_count=header_template.au_nal_count,
            pts=header_template.pts,
            au_size=au_size
        )

        # 序列化头 + payload
        packet = serialize_header(header) + frag_payload
        fragments.append(packet)

    return fragments

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 统计与日志
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

@dataclass
class SenderStats:
    """发送端统计"""
    au_sent: int = 0
    idr_sent: int = 0
    packets_sent: int = 0
    bytes_sent: int = 0
    send_errors: int = 0
    current_frame_seq: int = 0
    nals_parsed: int = 0
    au_parse_errors: int = 0
    last_print_time: float = 0.0
    start_time: float = 0.0

    def print_stats(self, log: logging.Logger):
        """打印统计信息"""
        elapsed = time.time() - self.start_time
        log.info(
            f"[Stats] AU={self.au_sent} IDR={self.idr_sent} "
            f"Packets={self.packets_sent} Bytes={self.bytes_sent} "
            f"Errors={self.send_errors} Seq={self.current_frame_seq} "
            f"Elapsed={elapsed:.1f}s"
        )

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# FFmpeg 集成
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def build_ffmpeg_cmd(input_path: str, fps: int, gop: int, bitrate: str) -> List[str]:
    """
    构建 FFmpeg 命令行
    使用 libx265 编码，输出 H.265 Annex B 到 stdout
    """
    # 解析码率（支持 M/K 后缀）
    bitrate_val = bitrate
    if bitrate.endswith('M'):
        bitrate_val = int(bitrate[:-1]) * 1_000_000
    elif bitrate.endswith('K'):
        bitrate_val = int(bitrate[:-1]) * 1_000
    else:
        bitrate_val = int(bitrate)

    # x265 参数：
    #   log-level=none：完全抑制日志输出
    #   info=0：禁用 x265 encoder info SEI（版本信息嵌入在 SEI NAL 中）
    x265_params = (
        f"keyint={gop}:min-keyint={gop}:no-scenecut=1:bframes=0:aud=1:repeat-headers=1:log-level=none:info=0"
    )

    cmd = [
        'ffmpeg',
        '-i', input_path,
        '-c:v', 'libx265',
        '-x265-params', x265_params,
        '-b:v', str(bitrate_val),
        '-r', str(fps),
        '-f', 'hevc',
        '-loglevel', 'quiet',  # 完全静默 FFmpeg 输出
        'pipe:1'
    ]

    return cmd

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 主发送类
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

class UdpSender:
    """UDP 发送端主类"""

    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.log = logging.getLogger('UdpSender')
        self.log.setLevel(args.log_level)

        # 统计
        self.stats = SenderStats(start_time=time.time(), last_print_time=time.time())

        # 缓存的参数集
        self.cached_vps: Optional[bytes] = None
        self.cached_sps: Optional[bytes] = None
        self.cached_pps: Optional[bytes] = None

        # AU 缓存（用于组帧）
        self.pending_nals: List[NalUnit] = []
        self.pending_pts: int = 0
        self.frame_seq: int = 0

        # UDP socket
        self.sock: Optional[socket.socket] = None
        self.dest_addr: Tuple[str, int] = None

        # FFmpeg 进程
        self.ffmpeg_proc: Optional[subprocess.Popen] = None

        # 运行状态
        self.running = False

        # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        # AU dump 调试功能
        # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        self.debug_dump_dir: Optional[str] = args.debug_dump_au_dir
        self.debug_dump_max: int = args.debug_dump_max_au
        self.debug_dump_count: int = 0
        self.debug_dump_manifest: List[Dict] = []

        if self.debug_dump_dir:
            os.makedirs(self.debug_dump_dir, exist_ok=True)
            self.log.info(f"AU dump enabled: dir={self.debug_dump_dir} max={self.debug_dump_max}")

    def _dump_au(self, au_payload: bytes, au: AccessUnit, frame_seq: int):
        """
        在分片前 dump AU payload
        AU_payload 格式: [4B nalu_len_be][nalu_bytes] × au_nal_count
        """
        if not self.debug_dump_dir or self.debug_dump_count >= self.debug_dump_max:
            return

        # 计算 SHA256
        sha256_hash = hashlib.sha256(au_payload).hexdigest()

        # 文件名: au_00000000.bin
        filename = f"au_{frame_seq:08d}.bin"
        filepath = os.path.join(self.debug_dump_dir, filename)

        # 写入 AU payload
        with open(filepath, 'wb') as f:
            f.write(au_payload)

        # 记录元信息
        meta = {
            'frame_seq': frame_seq,
            'is_idr': au.is_idr,
            'primary_nal_type': au.primary_nal_type,
            'au_nal_count': len(au.nals),
            'au_size': len(au_payload),
            'frag_total': (len(au_payload) + FRAGMENT_PAYLOAD_SIZE - 1) // FRAGMENT_PAYLOAD_SIZE,
            'pts': au.pts,
            'sha256': sha256_hash,
            'filename': filename
        }
        self.debug_dump_manifest.append(meta)
        self.debug_dump_count += 1

        self.log.debug(f"Dumped AU seq={frame_seq} size={len(au_payload)} hash={sha256_hash[:16]}...")

        # 达到上限时写入 manifest
        if self.debug_dump_count >= self.debug_dump_max:
            self._write_manifest()

    def _write_manifest(self):
        """写入元信息清单"""
        if not self.debug_dump_dir or not self.debug_dump_manifest:
            return

        manifest_path = os.path.join(self.debug_dump_dir, 'au_manifest.json')
        with open(manifest_path, 'w') as f:
            json.dump(self.debug_dump_manifest, f, indent=2)

        self.log.info(f"Wrote AU manifest: {manifest_path} ({len(self.debug_dump_manifest)} entries)")

    def setup(self):
        """初始化"""
        # 创建 UDP socket
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_SNDBUF, 8 * 1024 * 1024)

        port = CHANNEL_PORT_MAP[self.args.channel]
        self.dest_addr = (self.args.dest_ip, port)

        self.log.info(f"UDP socket ready: {self.dest_addr}")

        # 启动 FFmpeg
        cmd = build_ffmpeg_cmd(
            self.args.input,
            self.args.fps,
            self.args.gop,
            self.args.bitrate
        )
        self.log.info(f"FFmpeg cmd: {' '.join(cmd)}")

        try:
            self.ffmpeg_proc = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                bufsize=0
            )
            self.log.info("FFmpeg started")
        except Exception as e:
            self.log.error(f"Failed to start FFmpeg: {e}")
            raise

    def process_nal(self, nal: NalUnit, pts: int) -> Optional[AccessUnit]:
        """
        处理单个 NAL，按 AUD 边界组帧
        返回完整的 AU（如果 AUD 边界到达）
        """
        # 缓存参数集
        if nal.nal_type == H265NalType.VPS:
            self.cached_vps = nal.data
            self.log.debug(f"Cached VPS ({len(nal.data)} bytes)")
        elif nal.nal_type == H265NalType.SPS:
            self.cached_sps = nal.data
            self.log.debug(f"Cached SPS ({len(nal.data)} bytes)")
        elif nal.nal_type == H265NalType.PPS:
            self.cached_pps = nal.data
            self.log.debug(f"Cached PPS ({len(nal.data)} bytes)")

        # AUD 作为 AU 边界
        if nal.nal_type == H265NalType.AUD:
            # 收到 AUD，结束当前 AU
            au_to_return = None
            if self.pending_nals:
                au_to_return = build_au_from_nals(
                    self.pending_nals,
                    self.pending_pts,
                    self.cached_vps,
                    self.cached_sps,
                    self.cached_pps
                )
                self.pending_nals = []

            # 将 AUD 添加到新的 pending_nals（作为下一帧的第一个 NAL）
            # 注意：AUD 应该是每个 AU 的第一个 NAL
            self.pending_nals.append(nal)
            self.pending_pts = pts
            self.stats.nals_parsed += 1

            return au_to_return

        # 添加到待处理列表
        self.pending_nals.append(nal)
        self.pending_pts = pts
        self.stats.nals_parsed += 1

        return None

    def send_au(self, au: AccessUnit):
        """发送完整 AU"""
        if au is None:
            return

        # 序列化 AU
        au_payload = serialize_au(au)
        au_size = len(au_payload)

        if au_size == 0:
            self.log.warning("Empty AU, skip")
            self.stats.au_parse_errors += 1
            return

        # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        # AU dump（在分片前）
        # ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
        self._dump_au(au_payload, au, self.frame_seq)

        # 构建协议头模板
        header_template = ProtocolHeader(
            channel_id=self.args.channel,
            frame_seq=self.frame_seq,
            is_idr=1 if au.is_idr else 0,
            primary_nal_type=au.primary_nal_type,
            au_nal_count=len(au.nals),
            pts=au.pts,
            au_size=au_size
        )

        # 分片
        fragments = build_fragments(au_payload, header_template)

        # 发送
        for frag in fragments:
            try:
                self.sock.sendto(frag, self.dest_addr)
                self.stats.packets_sent += 1
                self.stats.bytes_sent += len(frag)
                # Pacing: 在分片之间添加小延迟，避免突发导致丢包
                if self.args.pacing_us > 0:
                    time.sleep(self.args.pacing_us / 1_000_000)
            except Exception as e:
                self.log.error(f"Send error: {e}")
                self.stats.send_errors += 1

        # 更新统计
        self.stats.au_sent += 1
        if au.is_idr:
            self.stats.idr_sent += 1
            self.log.info(f"Sent IDR AU seq={self.frame_seq} size={au_size} frags={len(fragments)}")
        else:
            self.log.debug(f"Sent AU seq={self.frame_seq} size={au_size} frags={len(fragments)}")

        self.stats.current_frame_seq = self.frame_seq
        self.frame_seq += 1  # AU 级递增

    def run(self):
        """主循环"""
        self.running = True
        self.setup()

        # 读取缓冲
        buffer = b''
        chunk_size = 4096

        try:
            while self.running:
                # 从 FFmpeg 读取数据
                chunk = self.ffmpeg_proc.stdout.read(chunk_size)
                if not chunk:
                    # FFmpeg 结束，处理 buffer 中剩余的 NAL
                    self.log.info("FFmpeg finished")

                    # 处理 buffer 中剩余的完整 NAL
                    if buffer:
                        nals = parse_nal_units(buffer)
                        if nals:
                            parsed_len = 0
                            for nal in nals:
                                parsed_len += nal.start_code_len + len(nal.data)
                                pts = self.frame_seq * (90000 // self.args.fps)
                                au = self.process_nal(nal, pts)
                                if au:
                                    self.send_au(au)
                            buffer = buffer[parsed_len:]

                        # 处理最后一个不完整的 NAL（如果有的话）
                        # 这是流的最后部分，需要特殊处理
                        if buffer:
                            # 尝试解析最后一个 NAL
                            last_nal_start, last_sc_len = find_start_code(buffer, 0)
                            if last_nal_start is not None:
                                last_nal_data = buffer[last_nal_start:]
                                if len(last_nal_data) > 0:
                                    nal_type = (last_nal_data[0] >> 1) & 0x3F
                                    last_nal = NalUnit(
                                        nal_type=nal_type,
                                        data=last_nal_data,
                                        start_code_len=last_sc_len
                                    )
                                    pts = self.frame_seq * (90000 // self.args.fps)
                                    au = self.process_nal(last_nal, pts)
                                    if au:
                                        self.send_au(au)
                    break

                buffer += chunk

                # 解析 NAL
                nals = parse_nal_units(buffer)

                if nals:
                    # 计算剩余未解析数据
                    # 找到最后一个 NAL 的结束位置
                    last_nal = nals[-1]
                    # 重新计算 buffer 位置（跳过已解析部分）
                    # 由于 parse_nal_units 返回不含起始码的 NAL，需要重新定位

                    # 简化处理：清空 buffer，让下次重新解析
                    # 实际上应该保留未完整解析的部分
                    # 这里采用更稳健的策略：保留最后可能不完整的部分

                    # 计算已解析的字节总数（包括起始码）
                    parsed_len = 0
                    for nal in nals:
                        parsed_len += nal.start_code_len + len(nal.data)

                    # 处理每个 NAL
                    for nal in nals:
                        # 生成 PTS（基于帧序号）
                        pts = self.frame_seq * (90000 // self.args.fps)

                        au = self.process_nal(nal, pts)
                        if au:
                            self.send_au(au)

                    # 清空已解析部分
                    buffer = buffer[parsed_len:]

                # 定期打印统计
                now = time.time()
                if now - self.stats.last_print_time >= 5.0:
                    self.stats.print_stats(self.log)
                    self.stats.last_print_time = now

        except Exception as e:
            self.log.error(f"Main loop error: {e}")
            raise

        finally:
            # 处理剩余 NAL（如果没有 AUD 结尾）
            if self.pending_nals:
                pts = self.frame_seq * (90000 // self.args.fps)
                au = build_au_from_nals(
                    self.pending_nals,
                    pts,
                    self.cached_vps,
                    self.cached_sps,
                    self.cached_pps
                )
                if au:
                    self.send_au(au)

            # 写入 AU dump manifest（如果未达到上限）
            self._write_manifest()

            # 最终统计
            self.stats.print_stats(self.log)

            # 清理
            if self.ffmpeg_proc:
                self.ffmpeg_proc.terminate()
                self.ffmpeg_proc.wait()

            if self.sock:
                self.sock.close()

            self.log.info("Sender stopped")

    def stop(self):
        """停止发送"""
        self.running = False

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# 命令行入口
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

def main():
    parser = argparse.ArgumentParser(description='UDP Video Sender')

    parser.add_argument('-i', '--input', required=True, help='Input video file')
    parser.add_argument('--dest-ip', required=True, help='Destination IP address')
    parser.add_argument('-c', '--channel', type=int, default=0, choices=[0,1,2,3],
                        help='Channel ID (0-3)')
    parser.add_argument('--fps', type=int, default=25, help='Frame rate')
    parser.add_argument('--gop', type=int, default=25, help='GOP size')
    parser.add_argument('--bitrate', default='4M', help='Bitrate (e.g., 4M, 2000K)')
    parser.add_argument('--pacing-us', type=int, default=0,
                        help='Microseconds to wait between fragments (0=disabled, try 100-500 for wireless)')
    parser.add_argument('--log-level', default='INFO',
                        choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                        help='Log level')

    # AU dump 调试参数
    parser.add_argument('--debug-dump-au-dir', default=None,
                        help='Directory to dump AU payloads for debugging')
    parser.add_argument('--debug-dump-max-au', type=int, default=100,
                        help='Maximum number of AU to dump (default: 100)')

    args = parser.parse_args()

    # 展开路径中的 ~ 符号
    args.input = os.path.expanduser(args.input)

    # 设置日志
    logging.basicConfig(
        format='%(asctime)s %(levelname)s %(message)s',
        level=args.log_level
    )

    sender = UdpSender(args)
    sender.run()

if __name__ == '__main__':
    main()