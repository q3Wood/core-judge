# core-judge 技术设计规格书

## 1. 系统目标
一个基于 Linux 的独立无头（Headless）评测核心，负责编译、沙箱运行并评判给定的代码与测试用例。

## 2. 状态码规范 (Verdicts)
定义程序可能返回的唯一结果状态：
- **AC** (Accepted): 答案完全正确
- **WA** (Wrong Answer): 输出与答案不一致
- **TLE** (Time Limit Exceeded): 运行超过 CPU 时间或真实时间
- **MLE** (Memory Limit Exceeded): 物理内存占用超过限制
- **RE** (Runtime Error): 发生异常信号（SIGSEGV, SIGFPE 等）或非 0 退出
- **CE** (Compile Error): 源码编译失败
- **SE** (System Error): 评测系统自身异常（如找不到测试数据）

## 3. 核心数据结构 (C++ 层面契约)
在编写逻辑前，定义模块之间传递的结构体：

```cpp
// 单个测试点的运行结果
struct CaseResult {
    std::string verdict; // AC, WA, TLE, MLE, RE
    int time_ms;         // 消耗的 CPU 时间
    int memory_kb;       // 消耗的最大常驻内存
    std::string message; // 补充说明 (如 RE 时的信号名)
};

// 整个评测任务的最终报告
struct FinalReport {
    std::string verdict; // 最终综合状态
    int max_time_ms;     // 所有点中最长耗时
    int peak_memory_kb;  // 所有点中最高内存
    std::vector<CaseResult> cases; // 各测试点详情
};
```

## 4. 命令行调用接口 (CLI Interface)
设计你预期的 CLI 参数形式：
`./core-judge --prob <题目路径> --src <源码路径> --lang <语言> [--json]`

## 5. 状态流转规则
- 编译失败 -> 立即终止，返回 CE
- 依次评测各测试用例：
  - 先检查退出信号与返回值 -> 判定 RE
  - 再检查耗时与内存 -> 判定 TLE / MLE
  - 正常退出且未超限 -> 进入 Checker 比对文件 -> 判定 AC / WA
- 汇总规则：ACM 赛制模式下，遇到首个非 AC 测试点即停止评测。

## 6. 模块划分与核心接口 (Module Interfaces)

系统划分为三个互不干扰的核心无状态模块：Compiler、Runner、Checker。

### 6.1 Compiler (编译模块)
- **核心职责**：将用户提交的源码文件编译成可执行文件；如果编译失败，收集编译错误日志。
- **不负责**：不负责运行程序，不负责检查测试用例。

#### 接口定义：
```cpp
// include/compiler.hpp

struct CompileConfig {
    std::string src_path;   // 源码路径，如 var/workspace/sub_1/main.cpp
    std::string out_path;   // 编译产物目标路径，如 var/workspace/sub_1/main
    std::string lang;       // 语言标识: "cpp", "c", etc.
    int time_limit_ms;      // 编译超时时间，防模板元编程挂起 (如 10000ms)
};

struct CompileResult {
    bool success;           // 是否编译成功
    std::string error_log;  // 编译报错信息 (CE 时的 stderr)
};

// 暴露的唯一核心函数
CompileResult compile(const CompileConfig& config);
```

---

### 6.2 Runner (沙箱运行模块)
- **核心职责**：使用 `fork()` 启动子进程，通过重定向将输入文件喂给被测程序，限制其时间和内存；程序结束后，通过 `wait4()` 返回真实的耗时、物理内存、退出码或终止信号。
- **不负责**：**坚决不负责比对答案**！即使用户程序输出了错误的答案，只要它在时空限制内正常退出，Runner 阶段都算成功执行。

#### 接口定义：
```cpp
// include/runner.hpp

struct RunConfig {
    std::string exe_path;   // 待执行二进制文件路径
    std::string in_file;    // 标准输入文件路径 (1.in)
    std::string out_file;   // 重定向输出文件路径 (1.out)
    int time_limit_ms;      // CPU 时间限制
    int memory_limit_kb;    // 内存限制
};

struct RunResult {
    int cpu_time_ms;        // 消耗的 CPU 时间
    int memory_kb;          // 峰值物理内存占用
    int exit_code;          // 正常退出时的退出码 (0, 1...)
    int signal;             // 异常退出时的信号 (如 SIGSEGV段错误, SIGXCPU超时; 正常为 0)
    bool is_timeout;        // 是否超时 (看门狗触发)
};

// 暴露的唯一核心函数
RunResult run_process(const RunConfig& config);
```

---

### 6.3 Checker (答案比对模块)
- **核心职责**：对比用户程序生成的 `user.out` 和题目预设的标准答案 `std.ans`。
- **不负责**：不管程序跑了多久、超没超内存，只对比两个文件内容。

#### 接口定义：
```cpp
// include/checker.hpp

enum class DiffResult {
    ACCEPTED,      // 完全一致 (忽略行末空格与空行)
    WRONG_ANSWER,  // 内容不匹配
    OUTPUT_LIMIT   // 输出文件异常巨大
};

// 暴露的唯一核心函数
DiffResult check_output(const std::string& user_out_path, const std::string& std_ans_path);
```