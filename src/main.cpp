#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <map>
#include <unistd.h>
#include <csignal>

#include "common.hpp"
#include "compiler.hpp"
#include "runner.hpp"
#include "checker.hpp"

namespace fs = std::filesystem;

namespace Color {
    const std::string RESET   = "\033[0m";
    const std::string RED     = "\033[31m";
    const std::string GREEN   = "\033[32m";
    const std::string YELLOW  = "\033[33m";
    const std::string BLUE    = "\033[34m";
    const std::string MAGENTA = "\033[35m";
    const std::string CYAN    = "\033[36m";
    const std::string BOLD    = "\033[1m";
}

struct CLIConfig {
    std::string prob_dir;
    std::string src_path;
    std::string lang = "cpp";
    int time_limit_ms = 1000;
    int memory_limit_mb = 256;
    bool json_output = false;
};

CLIConfig parse_arguments(int argc, char* argv[]) {
    CLIConfig config;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--prob-dir" && i + 1 < argc) config.prob_dir = argv[++i];
        else if (arg == "--src" && i + 1 < argc) config.src_path = argv[++i];
        else if (arg == "--lang" && i + 1 < argc) config.lang = argv[++i];
        else if (arg == "--time" && i + 1 < argc) config.time_limit_ms = std::stoi(argv[++i]);
        else if (arg == "--mem" && i + 1 < argc) config.memory_limit_mb = std::stoi(argv[++i]);
        else if (arg == "--json") config.json_output = true;
    }
    return config;
}

void print_colored_verdict(Verdict v) {
    switch (v) {
        case Verdict::AC:  std::cout << Color::GREEN << "[Accepted] " << Color::RESET; break;
        case Verdict::WA:  std::cout << Color::RED << "[Wrong Answer] " << Color::RESET; break;
        case Verdict::TLE: std::cout << Color::YELLOW << "[Time Limit Exceeded] " << Color::RESET; break;
        case Verdict::MLE: std::cout << Color::MAGENTA << "[Memory Limit Exceeded] " << Color::RESET; break;
        case Verdict::OLE: std::cout << Color::YELLOW << "[Output Limit Exceeded] " << Color::RESET; break;
        case Verdict::RE:  std::cout << Color::RED << "[Runtime Error] " << Color::RESET; break;
        case Verdict::CE:  std::cout << Color::CYAN << "[Compile Error] " << Color::RESET; break;
        default:           std::cout << Color::RED << "[System Error] " << Color::RESET; break;
    }
}

// 信号转具体可读原因
std::string get_signal_description(int sig, bool is_wall_timeout) {
    if (is_wall_timeout) return "Terminated by Watchdog (Wall Clock Timeout)";
    switch (sig) {
        case SIGSEGV: return "Segmentation fault (Invalid memory access / Null pointer)";
        case SIGFPE:  return "Floating point exception (Division by zero)";
        case SIGXCPU: return "CPU time limit exceeded";
        case SIGXFSZ: return "File size limit exceeded (Output bomb)";
        case SIGABRT: return "Aborted (Assertion failed or std::terminate)";
        case SIGKILL: return "Process killed (SIGKILL)";
        default:      return sig == 0 ? "Normal exit" : ("Killed by signal " + std::to_string(sig));
    }
}

// 辅助排序：优先按纯数字排序 (1, 2 ... 10)，非纯数字按字母序
bool natural_case_compare(const std::string& a, const std::string& b) {
    bool a_is_num = !a.empty() && std::all_of(a.begin(), a.end(), ::isdigit);
    bool b_is_num = !b.empty() && std::all_of(b.begin(), b.end(), ::isdigit);
    if (a_is_num && b_is_num) {
        return std::stoll(a) < std::stoll(b);
    }
    return a < b;
}

int main(int argc, char* argv[]) {
    CLIConfig config = parse_arguments(argc, argv);

    if (config.prob_dir.empty() || config.src_path.empty()) {
        std::cerr << "Usage: " << argv[0] << " --prob-dir <dir> --src <source_file> [--lang cpp] [--time 1000] [--mem 256] [--json]\n";
        return 1;
    }

    std::string run_id = std::to_string(getpid());
    fs::path workspace = fs::temp_directory_path() / ("core_judge_" + run_id);
    fs::create_directories(workspace);

    JudgeReport report;
    report.final_verdict = Verdict::AC;
    report.max_time_ms = 0;
    report.peak_memory_kb = 0;

    // 1. 编译阶段
    fs::path exe_path = workspace / "solution_bin";
    CompileConfig comp_cfg{config.src_path, exe_path.string(), config.lang, 10};

    if (!config.json_output) {
        std::cout << Color::BOLD << ">>> Compiling Source: " << config.src_path << " (" << config.lang << ")..." << Color::RESET << "\n";
    }

    CompileResult comp_res = Compiler::build(comp_cfg);
    if (!comp_res.ok) {
        report.final_verdict = Verdict::CE;
        report.compile_log = comp_res.log;

        if (config.json_output) {
            std::cout << "{\"verdict\":\"Compile Error\",\"compile_log\":\"" << comp_res.log << "\",\"details\":[]}\n";
        } else {
            print_colored_verdict(Verdict::CE);
            std::cout << "\n--- Compiler stderr ---\n" << comp_res.log << "------------------------\n";
        }
        fs::remove_all(workspace);
        return 0;
    }

    if (!config.json_output) {
        std::cout << Color::GREEN << ">>> Compilation Succeeded!" << Color::RESET << "\n\n";
        std::cout << Color::BOLD << ">>> Evaluating Test Cases:" << Color::RESET << "\n";
    }

    // 2. 收集并配对测试用例 (.in 与 .ans/.out)
    std::map<std::string, std::pair<fs::path, fs::path>> raw_cases;
    for (const auto& entry : fs::directory_iterator(config.prob_dir)) {
        if (!entry.is_regular_file()) continue;
        std::string ext = entry.path().extension().string();
        std::string stem = entry.path().stem().string();

        if (ext == ".in") raw_cases[stem].first = entry.path();
        else if (ext == ".ans" || ext == ".out") raw_cases[stem].second = entry.path();
    }

    // 转为按自然数排序的列表
    std::vector<std::pair<std::string, std::pair<fs::path, fs::path>>> sorted_cases(raw_cases.begin(), raw_cases.end());
    std::sort(sorted_cases.begin(), sorted_cases.end(), [](const auto& a, const auto& b) {
        return natural_case_compare(a.first, b.first);
    });

    int case_id = 0;
    for (const auto& [name, files] : sorted_cases) {
        case_id++;
        if (files.first.empty() || files.second.empty()) continue;

        fs::path user_out = workspace / (name + ".user.out");
        SandboxConfig run_cfg;
        run_cfg.exe_path = exe_path.string();
        run_cfg.stdin_file = files.first.string();
        run_cfg.stdout_file = user_out.string();
        run_cfg.time_limit_ms = config.time_limit_ms;
        run_cfg.memory_limit_kb = config.memory_limit_mb * 1024;

        ExecutionResult exec_res = SandboxRunner::run(run_cfg);

        TestCaseResult case_result;
        case_result.id = case_id;
        case_result.time_ms = exec_res.cpu_time_ms;
        case_result.memory_kb = exec_res.memory_kb;
        case_result.exit_code = exec_res.exit_code;
        case_result.signal = exec_res.signal;
        case_result.message = get_signal_description(exec_res.signal, exec_res.is_wall_timeout);

        report.max_time_ms = std::max(report.max_time_ms, exec_res.cpu_time_ms);
        report.peak_memory_kb = std::max(report.peak_memory_kb, exec_res.memory_kb);

        // 状态判定
        if (exec_res.is_wall_timeout || exec_res.signal == SIGXCPU) {
            case_result.verdict = Verdict::TLE;
        } else if (exec_res.signal == SIGXFSZ) {
            case_result.verdict = Verdict::OLE;
        } else if (exec_res.signal != 0 || exec_res.exit_code != 0) {
            case_result.verdict = Verdict::RE;
            if (exec_res.signal == 0) case_result.message = "Non-zero exit code: " + std::to_string(exec_res.exit_code);
        } else if (exec_res.memory_kb > config.memory_limit_mb * 1024) {
            case_result.verdict = Verdict::MLE;
            case_result.message = "Memory limit exceeded";
        } else {
            CheckStatus diff_status = Checker::strict_diff(user_out.string(), files.second.string());
            if (diff_status == CheckStatus::MATCH) {
                case_result.verdict = Verdict::AC;
                case_result.message = "Correct answer";
            } else {
                case_result.verdict = Verdict::WA;
                case_result.message = "Output mismatch";
            }
        }

        report.details.push_back(case_result);

        // 终端可视化展示
        if (!config.json_output) {
            std::cout << "  Point #" << std::left << std::setw(3) << case_id << " [" << name << "]: ";
            print_colored_verdict(case_result.verdict);
            std::cout << " | Time: " << std::setw(4) << case_result.time_ms << "ms"
                      << " | Mem: " << std::setw(6) << case_result.memory_kb << "KB";
            if (case_result.verdict != Verdict::AC) {
                std::cout << " | Reason: " << Color::RED << case_result.message << Color::RESET;
            }
            std::cout << "\n";
        }

        if (case_result.verdict != Verdict::AC) {
            report.final_verdict = case_result.verdict;
            break;
        }
    }

    fs::remove_all(workspace);

    // 3. 最终汇总输出
    if (config.json_output) {
        std::cout << "{"
                  << "\"verdict\":\"" << verdict_to_string(report.final_verdict) << "\","
                  << "\"max_time_ms\":" << report.max_time_ms << ","
                  << "\"peak_memory_kb\":" << report.peak_memory_kb << ","
                  << "\"details\":[";
        for (size_t i = 0; i < report.details.size(); ++i) {
            const auto& d = report.details[i];
            std::cout << "{"
                      << "\"id\":" << d.id << ","
                      << "\"verdict\":\"" << verdict_to_string(d.verdict) << "\","
                      << "\"time_ms\":" << d.time_ms << ","
                      << "\"memory_kb\":" << d.memory_kb << ","
                      << "\"exit_code\":" << d.exit_code << ","
                      << "\"signal\":" << d.signal << ","
                      << "\"message\":\"" << d.message << "\""
                      << "}" << (i + 1 < report.details.size() ? "," : "");
        }
        std::cout << "]}\n";
    } else {
        std::cout << "\n" << Color::BOLD << "================ Final Report ================" << Color::RESET << "\n";
        std::cout << "Final Verdict: ";
        print_colored_verdict(report.final_verdict);
        std::cout << "\nMax CPU Time : " << report.max_time_ms << " ms\n";
        std::cout << "Peak Memory  : " << report.peak_memory_kb << " KB (" << (report.peak_memory_kb / 1024.0) << " MB)\n";
        std::cout << Color::BOLD << "==============================================" << Color::RESET << "\n";
    }

    return 0;
}