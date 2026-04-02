cmake_minimum_required(VERSION 3.10)

# 添加单元测试
add_executable(test_unit
    test_unit.cpp
    ${CMAKE_SOURCE_DIR}/src/crc16.cpp
    ${CMAKE_SOURCE_DIR}/src/h265_nal.cpp
    ${CMAKE_SOURCE_DIR}/src/protocol.cpp
)

target_include_directories(test_unit PRIVATE ${CMAKE_SOURCE_DIR}/include)

# 添加测试
enable_testing()
add_test(NAME UnitTests COMMAND test_unit)

# 打印配置
message(STATUS "Test executable: test_unit")