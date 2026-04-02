cmake_minimum_required(VERSION 3.10)
project(udp_sender VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0")

# 源文件
set(SOURCES
    src/main.cpp
    src/crc16.cpp
    src/h265_nal.cpp
    src/protocol.cpp
    src/udp_sender.cpp
)

# 可执行文件
add_executable(sender ${SOURCES})

# 头文件目录
target_include_directories(sender PRIVATE ${CMAKE_SOURCE_DIR}/include)

# 链接库
target_link_libraries(sender PRIVATE)

# 安装
install(TARGETS sender DESTINATION bin)

# 测试
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()

# 打印配置
message(STATUS "")
message(STATUS "UDP Video Sender - Configuration")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "")