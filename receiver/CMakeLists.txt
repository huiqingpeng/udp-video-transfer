cmake_minimum_required(VERSION 3.16)
project(udp_receiver VERSION 1.1.0 LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")

# 头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# GStreamer (可选 - 用于硬件解码)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
option(DISABLE_GSTREAMER "Force disable GStreamer (for testing)" OFF)

if(NOT DISABLE_GSTREAMER)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GSTREAMER QUIET
            gstreamer-1.0>=1.14
            gstreamer-app-1.0>=1.14
        )
    endif()
endif()

if(GSTREAMER_FOUND)
    message(STATUS "GStreamer found, decode mode enabled")
    include_directories(${GSTREAMER_INCLUDE_DIRS})
    link_directories(${GSTREAMER_LIBRARY_DIRS})
    add_definitions(-DHAS_GSTREAMER)
else()
    message(STATUS "GStreamer not found, decode mode disabled (file mode only)")
endif()

# 源文件
set(SOURCES
    src/main.cpp
    src/crc16.cpp
    src/packet_header.cpp
    src/reassembly.cpp
    src/annexb_writer.cpp
    src/metrics.cpp
    src/idr_fsm.cpp
    src/channel_receiver.cpp
    src/multi_channel_receiver.cpp
)

# GStreamer 解码器（可选）
if(GSTREAMER_FOUND)
    list(APPEND SOURCES src/gst_decoder.cpp)
endif()

# 可执行文件
add_executable(receiver ${SOURCES})

# 链接库
find_package(Threads REQUIRED)

target_link_libraries(receiver PRIVATE Threads::Threads)

# GStreamer 链接（可选）
if(GSTREAMER_FOUND)
    target_link_libraries(receiver PRIVATE ${GSTREAMER_LIBRARIES})
    target_compile_options(receiver PRIVATE ${GSTREAMER_CFLAGS_OTHER})
endif()

# 创建输出目录
add_custom_command(TARGET receiver POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${PROJECT_SOURCE_DIR}/dump
    COMMENT "Creating dump directory"
)