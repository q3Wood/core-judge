#include "runner.hpp"

#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <signal.h>

#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <algorithm>
#include <iostream>

ExecutionResult SandboxRunner::run(const SandboxConfig& config) {
    // 1. Fork 创建子进程
    pid_t pid = fork();

    if (pid < 0) {
        // 系统错误：fork 失败
        return {-1, -1, -1, -1, false};
    }

    if (pid == 0) {
        // ================= 子进程：配置沙箱并运行 =================

        // 1.1 输入输出重定向
        if (!config.stdin_file.empty()) {
            int fd_in = open(config.stdin_file.c_str(), O_RDONLY);
            if (fd_in < 0) _exit(101);
            dup2(fd_in, STDIN_FILENO);
            close(fd_in);
        }

        if (!config.stdout_file.empty()) {
            int fd_out = open(config.stdout_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_out < 0) _exit(102);
            dup2(fd_out, STDOUT_FILENO);
            close(fd_out);
        }

        if (!config.stderr_file.empty()) {
            int fd_err = open(config.stderr_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd_err < 0) _exit(103);
            dup2(fd_err, STDERR_FILENO);
            close(fd_err);
        }

        // 1.2 注入系统资源限制 (setrlimit)
        
        // (a) CPU 时间限制 (秒级软限制)
        struct rlimit rl_cpu;
        rl_cpu.rlim_cur = (config.time_limit_ms / 1000) + 1;
        rl_cpu.rlim_max = rl_cpu.rlim_cur + 1;
        setrlimit(RLIMIT_CPU, &rl_cpu);

        // (b) 输出文件大小限制 (防止磁盘炸弹，超出触发 SIGXFSZ)
        struct rlimit rl_fsize;
        rl_fsize.rlim_cur = config.fsize_limit_mb * 1024 * 1024;
        rl_fsize.rlim_max = rl_fsize.rlim_cur;
        setrlimit(RLIMIT_FSIZE, &rl_fsize);

        // (c) 运行栈大小限制 (防止爆栈段错误，给 128MB)
        struct rlimit rl_stack;
        rl_stack.rlim_cur = 128 * 1024 * 1024;
        rl_stack.rlim_max = rl_stack.rlim_cur;
        setrlimit(RLIMIT_STACK, &rl_stack);

        // (d) 禁止生成 core dump 文件
        struct rlimit rl_core;
        rl_core.rlim_cur = 0;
        rl_core.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &rl_core);

        // 1.3 执行目标程序
        char* args[] = { const_cast<char*>(config.exe_path.c_str()), nullptr };
        char* envp[] = { nullptr }; // 清空环境变量
        execve(config.exe_path.c_str(), args, envp);

        // execve 只有失败才会执行到这里
        _exit(127);
    } else {
        // ================= 父进程：看门狗监控与资源采集 =================

        std::atomic<bool> is_wall_timeout{false};
        std::atomic<bool> child_finished{false};
        std::mutex cv_m;
        std::condition_variable cv;

        // 计算真实时钟容忍上限：时限的 2 倍或至少时限+1000ms
        int wall_limit_ms = std::max(config.time_limit_ms * 2, config.time_limit_ms + 1000);

        // 启动独立看门狗线程
        std::thread watchdog([&]() {
            std::unique_lock<std::mutex> lk(cv_m);
            // 等待子进程退出信号，超时后唤醒
            if (!cv.wait_for(lk, std::chrono::milliseconds(wall_limit_ms), [&]() { return child_finished.load(); })) {
                // 超时仍未结束：看门狗立即发 SIGKILL 抹杀子进程
                is_wall_timeout.store(true);
                kill(pid, SIGKILL);
            }
        });

        // 阻塞等待子进程并采集内核统计 (rusage)
        int status;
        struct rusage usage;
        wait4(pid, &status, 0, &usage);

        // 子进程已退出，唤醒并回收看门狗线程
        child_finished.store(true);
        cv.notify_one();
        if (watchdog.joinable()) {
            watchdog.join();
        }

        // 2. 组装结果
        ExecutionResult res;
        // 计算耗时：用户态 CPU + 内核态 CPU (毫秒)
        res.cpu_time_ms = (usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) * 1000 +
                          (usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000;
        
        // 峰值常驻物理内存 (Linux 下单位是 KiB)
        res.memory_kb = usage.ru_maxrss;
        res.is_wall_timeout = is_wall_timeout.load();

        if (WIFEXITED(status)) {
            res.exit_code = WEXITSTATUS(status);
            res.signal = 0;
        } else if (WIFSIGNALED(status)) {
            res.exit_code = -1;
            res.signal = WTERMSIG(status); // 获取触发信号 (如 11 为 SIGSEGV, 9 为 SIGKILL)
        }

        return res;
    }
}