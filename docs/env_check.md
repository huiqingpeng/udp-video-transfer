# UDP 视频传输系统环境检查报告

检查时间：2026-04-01 14:16:29

## 本机环境（Jetson Orin Nano）

### 编译工具
- GCC: gcc (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
- G++: g++ (Ubuntu 11.4.0-1ubuntu1~22.04.3) 11.4.0
- CMake: cmake version 3.22.1

### 视频工具
- ffplay: /usr/bin/ffplay (可用)
- ffprobe: /usr/bin/ffprobe (ffprobe version 4.4.2-0ubuntu0.22.04.1)

### 网络
- 本机 IP: 192.168.8.136 (主要网络接口)
- 其他 IP: 100.101.72.116, 172.17.0.1 (docker bridge)

### 其他工具
- sshpass: /usr/bin/sshpass (sshpass 1.09)

## 远端环境（192.168.8.214）

### FFmpeg
- 版本: ffmpeg version 4.2.7-0ubuntu0.1
- 构建信息: built with gcc 9 (Ubuntu 9.4.0-1ubuntu1~20.04.1)
- libx265 支持: **已启用** (--enable-libx265)
- 编码器列表: V..... libx265 libx265 H.265 / HEVC (codec hevc)

### Python
- Python3: Python 3.8.10
- pip3: pip 20.0.2 from /usr/lib/python3/dist-packages/pip (python 3.8)

### 网络
- 主 IP 地址: 192.168.8.214
- 其他 IP: 192.168.153.133, 192.168.153.141

### 系统信息
- OS: Ubuntu 20.04 (根据 gcc 版本推断)
- 架构: x86_64

## UDP 连通性测试

- 测试结果: **成功**
- 测试方式: 本机 nc -ul 5000 监听，远端 nc -u 发送测试包
- 测试包内容: "udp_test_packet"
- 确认: 本机成功收到来自远端的 UDP 数据包

## 环境对照表

| 项目 | 要求 | 本机状态 | 远端状态 | 结论 |
|------|------|----------|----------|------|
| GCC/C++ | C++17 支持 | GCC 11.4 ✓ | 不需要 | 满足 |
| CMake | 3.16+ | 3.22.1 ✓ | 不需要 | 满足 |
| ffplay/ffprobe | 可用 | ✓ | 不需要 | 满足 |
| FFmpeg | 4.x+ | 不需要 | 4.2.7 ✓ | 满足 |
| libx265 | 支持 H.265 编码 | 不需要 | ✓ | 满足 |
| Python3 | 3.8+ | 不需要 | 3.8.10 ✓ | 满足 |
| pip3 | 可用 | 不需要 | ✓ | 满足 |
| UDP 连通 | 两机可达 | ✓ | ✓ | 满足 |

## 结论

**环境满足 M1 开发要求**

所有必需的开发工具和依赖均已确认可用：
1. 本机 Jetson 可编译 C++17 接收端程序
2. 本机具备 ffplay/ffprobe 用于验证输出文件
3. 远端具备 FFmpeg + libx265 用于 H.265 编码
4. 远端具备 Python 3.8 用于发送端脚本
5. 两机之间 UDP 5000 端口可正常通信

## 下一步

可进入 M1 开发阶段：
- 发送端：实现 sender/udp_sender.py
- 接收端：实现 receiver/ 目录下的 C++ 代码

## 备注

- 两机处于同一 192.168.8.x 子网，适合直连测试
- 远端 FFmpeg 已支持 libx264 和 libx265，可用于 H.264/H.265 编码测试
- 本机 ffprobe 版本 4.4.2，支持 H.265 码流分析