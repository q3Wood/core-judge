#pragma once
#include "common.hpp"
#include <string>

struct SandboxConfig {
    std::string exe_path;
    std::string stdin_file;     // 重定向标准输入
    std::string stdout_file;    // 重定向标准输出
    std::string stderr_file;    // 重定向标准错误
    int time_limit_ms;          // CPU 时间限制
    int memory_limit_kb;        // 内存限制
    int fsize_limit_mb = 32;    // 输出文件硬限制
};

struct ExecutionResult {
    int cpu_time_ms;
    int memory_kb;
    int exit_code;
    int signal;            // 导致的信号（0表示正常退出）
    bool is_wall_timeout;       // 看门狗触发
};

class SandboxRunner {
public:
    // 执行代码，内部完成 fork -> setrlimit -> execve 以及 Watchdog 监控
    static ExecutionResult run(const SandboxConfig& config);
};