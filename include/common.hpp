#pragma once

#include <string>
#include <vector>

// 评测最终状态码
enum class Verdict {
    AC,   // Accepted 通过
    WA,   // Wrong Answer 答案错误
    TLE,  // Time Limit Exceeded 超时
    MLE,  // Memory Limit Exceeded 超内存
    OLE,  // Output Limit Exceeded 输出超限
    RE,   // Runtime Error 运行时错误 (段错误/浮点错误等)
    CE,   // Compile Error 编译错误
    SE    // System Error 评测系统内部错误
};

// 工具函数：把枚举转为字符串方便输出展示
inline std::string verdict_to_string(Verdict v) {
    switch (v) {
        case Verdict::AC:  return "Accepted";
        case Verdict::WA:  return "Wrong Answer";
        case Verdict::TLE: return "Time Limit Exceeded";
        case Verdict::MLE: return "Memory Limit Exceeded";
        case Verdict::OLE: return "Output Limit Exceeded";
        case Verdict::RE:  return "Runtime Error";
        case Verdict::CE:  return "Compile Error";
        case Verdict::SE:  return "System Error";
        default:           return "Unknown";
    }
}

// 单个测试点的运行结果
struct TestCaseResult {
    int id;               // 测试点编号 (1, 2, 3...)
    Verdict verdict;      // 状态
    int time_ms;          // 消耗的 CPU 时间 (毫秒)
    int memory_kb;        // 消耗的常驻物理内存 (KiB)
    int exit_code;        // 退出码
    int signal;           // 终止信号 (如 11 代表 SIGSEGV 段错误)
    std::string message;  // 补充信息
};

// 整个评测任务的最终总报告
struct JudgeReport {
    Verdict final_verdict;              // 最终结果 (取优先级最高的非 AC)
    int max_time_ms;                    // 所有点中最长耗时
    int peak_memory_kb;                 // 所有点中峰值内存
    std::string compile_log;            // 编译日志 (CE 时展示)
    std::vector<TestCaseResult> details;// 各测试点详细明细
};