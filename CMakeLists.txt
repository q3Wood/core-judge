cmake_minimum_required(VERSION 3.16)
project(core-judge CXX)

# 使用现代 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 开启标准告警，帮助写出高品质代码
add_compile_options(-Wall -Wextra -pthread)

# 引入 include 目录
include_directories(include)

# 扫描源码
set(SOURCES
    src/main.cpp
    src/compiler.cpp
    src/runner.cpp
    src/checker.cpp
)

# 生成可执行文件
add_executable(core-judge ${SOURCES})