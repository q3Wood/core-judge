#pragma once
#include "common.hpp"
#include <string>

struct CompileConfig {
    std::string src_path;       // 源码路径
    std::string dest_exe_path;  // 编译产物输出全路径
    std::string lang;           // "cpp" | "c" | "py"
    int time_limit_sec = 10;    // 编译超时时间
};

struct CompileResult {
    bool ok;
    std::string log;            // stderr 输出
};

class Compiler {
public:
    static CompileResult build(const CompileConfig& config);
};