#include "compiler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <vector>
#include <array>
#include <iostream>

CompileResult Compiler::build(const CompileConfig& config) {
    // 1. 准备管道 (Pipe)，用于将编译器的 stderr (错误输出) 读回父进程
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        return {false, "System Error: Failed to create pipe for compiler."};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fd[0]);
        close(pipe_fd[1]);
        return {false, "System Error: Failed to fork compiler process."};
    }

    if (pid == 0) {
        // ================= 子进程：执行编译器命令 =================
        close(pipe_fd[0]); // 子进程只写不读，关闭读端

        // 重定向 stdout 和 stderr 到管道的写入端
        dup2(pipe_fd[1], STDOUT_FILENO);
        dup2(pipe_fd[1], STDERR_FILENO);
        close(pipe_fd[1]);

        // 限制编译时间，防止超大模板递归卡死系统
        struct rlimit rl_cpu;
        rl_cpu.rlim_cur = config.time_limit_sec;
        rl_cpu.rlim_max = config.time_limit_sec + 1;
        setrlimit(RLIMIT_CPU, &rl_cpu);

        // 构造编译参数
        std::vector<std::string> args_str;
        if (config.lang == "cpp") {
            args_str = {"g++", "-O2", "-std=c++17", config.src_path, "-o", config.dest_exe_path};
        } else if (config.lang == "c") {
            args_str = {"gcc", "-O2", config.src_path, "-o", config.dest_exe_path};
        } else {
            std::cerr << "Unsupported language: " << config.lang << "\n";
            _exit(1);
        }

        // 转为 execvp 所需的 char* 数组
        std::vector<char*> args;
        for (const auto& s : args_str) {
            args.push_back(const_cast<char*>(s.c_str()));
        }
        args.push_back(nullptr);

        // 替换镜像为编译器
        execvp(args[0], args.data());

        // 如果 execvp 执行失败 (如没安装 g++)
        std::cerr << "Failed to execute compiler binary.\n";
        _exit(1);
    } else {
        // ================= 父进程：读取编译结果 =================
        close(pipe_fd[1]); // 父进程只读不写，关闭写端

        // 从管道读取编译器吐出来的所有日志
        std::string compile_log;
        std::array<char, 1024> buffer;
        ssize_t bytes_read;
        while ((bytes_read = read(pipe_fd[0], buffer.data(), buffer.size())) > 0) {
            compile_log.append(buffer.data(), bytes_read);
        }
        close(pipe_fd[0]);

        // 等待编译子进程结束
        int status;
        waitpid(pid, &status, 0);

        // 检查编译退出状态
        if (WIFEXITED(status)) {
            int exit_code = WEXITSTATUS(status);
            if (exit_code == 0) {
                return {true, compile_log}; // 编译成功
            } else {
                return {false, compile_log}; // 编译报错 (CE)
            }
        } else if (WIFSIGNALED(status)) {
            // 被信号中断 (如编译超时触发 SIGXCPU)
            return {false, "Compiler killed by signal (possibly compile timeout)."};
        }

        return {false, "Unknown compiler error."};
    }
}