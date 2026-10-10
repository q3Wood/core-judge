# core-judge 核心评测引擎技术架构规格书 (RFC-001)

| 文档版本 | 状态 | 目标语言 | 运行环境 |
| :--- | :--- | :--- | :--- |
| v1.0.0 | **Draft / Ready for Implementation** | C++17 / C++20 | Linux (Kernel >= 5.4, 推荐 cgroup v2) |

---

## 1. 架构目标与非目标 (Scope & Boundaries)

### 1.1 核心目标 (Goals)
1. **执行隔离 (Isolation)**：提供进程级安全沙盒，严防越权读写宿主机文件、发起网络外联与恶意系统拒绝服务攻击。
2. **高精度资源计量 (Resource Metering)**：
   * 时间计量精度：毫秒级（区分用户态/内核态 CPU 时间与真实物理时间）。
   * 内存计量精度：KiB 级（追踪物理常驻内存 Peak RSS，兼容多语言运行时）。
3. **无状态与可重入 (Stateless & Idempotent)**：评测引擎作为纯单机 CLI / 子进程运行，不产生残留文件，崩溃不影响宿主机。
4. **确定性判题 (Deterministic Verdict)**：完备的状态机与故障映射，准确区分 TLE、MLE、OLE、RE。

### 1.2 非目标 (Non-Goals)
* 本系统**不**负责 Web 接口路由与鉴权（由上层 API 网关处理）。
* 本系统**不**负责题目数据的持久化同步（由外部挂载或预热同步）。

---

## 2. 评测系统生命周期与架构设计

```text
               CLI / 上层调用者
                      │
                      ▼ (传入参数: 源码, 测试用例目录, 时空限制)
      ┌──────────────────────────────────────────────┐
      │               core-judge 调度核心             │
      └──────────────────────┬───────────────────────┘
                             │
            ┌────────────────┴────────────────┐
            ▼                                 ▼
      [1. Compiler]                     [2. Workspace]
      编译目标语言源码                   隔离创建 /tmp/core_judge_<uuid>
      捕获 CE 错误流                     安全降权目录 (chmod 711)
            │                                 │
            └────────────────┬────────────────┘
                             ▼
                      [3. Sandbox Runner]
                      ┌──────────────────────┐
                      │ 父进程: Watchdog 线程 │ (真实时间计时器 SIGKILL)
                      └──────────┬───────────┘
                                 │ fork() / clone()
                      ┌──────────▼───────────┐
                      │ 子进程: Target Proc   │
                      │  - 重定向 stdin/out  │
                      │  - setrlimit 防护墙  │
                      │  - Seccomp 系统过滤  │
                      │  - setuid(nobody)    │
                      │  - execve 执行用户代码 │
                      └──────────┬───────────┘
                                 │ wait4() / 收集 rusage
                                 ▼
                      [4. Output Checker]
                      比对 out.txt 与 ans.txt (Token 流式/行规则)
                             │
                             ▼
                      [5. Result Aggregator]
                      汇总生成 JSON 评测报告 -> 清理 Workspace
```

---

## 3. Linux 内核级安全与沙箱防御规范 (The Security Model)

### 3.1 资源防御矩阵 (Resource Limits via `setrlimit`)

子进程在执行 `execve()` 之前，必须在内核层注入以下硬性限制：

| 资源宏 | 建议配置值 | 防御目的 |
| :--- | :--- | :--- |
| `RLIMIT_CPU` | `(time_limit_ms / 1000) + 1` 秒 | 防止 CPU 密集型死循环（内核发送 `SIGXCPU`） |
| `RLIMIT_FSIZE` | `32 * 1024 * 1024` (32 MiB) | 防止死循环输出打爆磁盘（内核发送 `SIGXFSZ` -> OLE） |
| `RLIMIT_NPROC` | `1` (或使用 cgroup pids.max) | **彻底封死 Fork 炸弹**，禁止衍生任何子进程 |
| `RLIMIT_STACK` | `128 * 1024 * 1024` (128 MiB) | 扩大默认调用栈，防止复杂 DFS/递归导致非预期爆栈段错误 |
| `RLIMIT_CORE` | `0` | 禁止生成 `core dump` 崩溃转储文件，避免耗尽磁盘 |

### 3.2 双时钟模型与看门狗 (Dual-Timer & Watchdog)
针对代码调用 `sleep()` 导致 CPU 时钟失效的问题，引入**双时钟仲裁机制**：
* **CPU Time (内核统计)**：`ru_utime + ru_stime`。达到限制即判 `TLE`。
* **Wall-Clock Time (真实时间)**：父进程启动 `std::thread` 监控线程（Watchdog）。
  * 设超时阈值为：`Real Time Limit = max(time_limit_ms * 2, time_limit_ms + 1000)`。
  * 若监控线程超时触发，立即对目标进程发送 `kill(pid, SIGKILL)`，并将结果标记为 `Wall Time Limit Exceeded`。

### 3.3 物理内存监控策略 (Memory Measurement)
* **抛弃** `RLIMIT_AS`：禁止使用虚拟地址空间限制，避免 JVM、Go、C++20 模块动态链接器初始化失败。
* **策略**：放行内存分配，通过 `wait4()` 返回的 `struct rusage` 中获取 `ru_maxrss`（Linux 下单位为 **KiB**）。
* **判定**：若 `ru_maxrss > memory_limit_kb`，即便程序正常退出，也覆盖判定为 `MLE`。

### 3.4 权限降级与文件隔离 (Privilege Dropping)
1. **降权**：评测引擎若以 `root` 或特权用户运行，子进程在 `execve` 前必须调用 `setgid(nogroup)` 及 `setuid(nobody)`。
2. **工作目录约束**：
   * 输入文件（`in.txt`）以**只读模式**（`O_RDONLY`）挂载至标准输入。
   * 用户程序所在工作空间通过 `chmod 755` 限制其仅能在当前指定目录写入，严禁写穿上层目录。

---

## 4. 判题状态机与故障映射规范 (Verdict Matrix)

任何一次运行必须确定性地收敛于以下状态之一：

| 最终状态代码 | 全称 | 触发条件判断流 |
| :--- | :--- | :--- |
| **AC** | Accepted | 退出码为 0，无异常信号，时空未超限，Checker 比对完全一致 |
| **WA** | Wrong Answer | 退出码为 0，无异常信号，时空未超限，Checker 比对不一致 |
| **TLE** | Time Limit Exceeded | 信号为 `SIGXCPU`，或 Watchdog 真实时间看门狗超时触发 |
| **MLE** | Memory Limit Exceeded | `res.memory_kb > config.memory_limit_kb` |
| **OLE** | Output Limit Exceeded | 捕获到信号 `SIGXFSZ`（超大文件写入被内核截断） |
| **RE** | Runtime Error | 捕获异常退出信号（`SIGSEGV` 段错误、`SIGFPE` 除零、`SIGABRT` 断言失败等），或退出码非 0 |
| **CE** | Compile Error | 编译器在限定时间内退出码非 0 |
| **SE** | System Error | 评测机自身系统调用失败（如 `fork` 失败、测试文件不存在） |

---

## 5. 核心模块与 C++ API 契约设计

代码必须严格遵循单向依赖，头文件存放于 `include/`，禁止交叉引用。

### 5.1 核心数据结构 (`include/types.hpp`)
```cpp
#pragma once
#include <string>
#include <vector>

enum class Verdict {
    AC, WA, TLE, MLE, OLE, RE, CE, SE
};

// 单测试点最终结果
struct TestCaseResult {
    int id;
    Verdict verdict;
    int time_ms;        // CPU 耗时
    int memory_kb;      // 内存峰值 (KiB)
    int exit_code;      // 进程退出码
    int signal;         // 引发终止的信号量 (若有)
    std::string message;// 调试信息 (如 RE 时的信号名)
};

// 完整评测报告
struct JudgeReport {
    Verdict final_verdict;
    int max_time_ms;
    int peak_memory_kb;
    std::string compile_log;
    std::vector<TestCaseResult> details;
};
```

### 5.2 编译模块 (`include/compiler.hpp`)
```cpp
#pragma once
#include "types.hpp"
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
```

### 5.3 沙箱运行模块 (`include/runner.hpp`)
```cpp
#pragma once
#include "types.hpp"
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
    int signal;
    bool is_wall_timeout;       // 看门狗触发
};

class SandboxRunner {
public:
    // 执行代码，内部完成 fork -> setrlimit -> execve 以及 Watchdog 监控
    static ExecutionResult run(const SandboxConfig& config);
};
```

### 5.4 答案校验器 (`include/checker.hpp`)
```cpp
#pragma once
#include <string>

enum class CheckStatus {
    MATCH, MISMATCH, FORMAT_ERROR
};

class Checker {
public:
    // 基础比对：支持忽略行末空格与尾部连续换行
    static CheckStatus strict_diff(const std::string& user_file, const std::string& ans_file);
};
```

### 5.5 主入口与流程编排器 (`src/main.cpp`)

- **核心职责 (Responsibilities)**：
  1. **CLI 参数解析**：解析命令行传入的参数，校验参数合法性（如测试目录是否存在）。
  2. **工作空间生命周期管理 (RAII)**：创建临时的独立沙箱工作区（如 `/tmp/core_judge_xxxx`），并在程序退出时确保清理。
  3. **流水线调度 (Pipeline Orchestration)**：
     - 步骤 1：触发编译。若失败，直接截获并返回 CE 报告。
     - 步骤 2：加载题目测试用例，按编号遍历评测。
     - 步骤 3：对每个测试点执行沙箱运行，并交由比对器判定。
     - 步骤 4：根据赛制规则（如 ACM 规则首错即止）决定是否提前熔断。
  4. **结果聚合与呈现**：负责最终评测结果的格式化，支持友好的终端彩色文本或供外部程序解析的 JSON 格式。
- **边界约束**：`main.cpp` 严禁直接调用任何底层的 Linux 系统调用（如 `fork`, `execve`），所有操作必须委托给对应的子模块。

#### 内部调度数据流结构：
```cpp
// 命令行解析后的运行配置
struct CLIConfig {
    std::string prob_dir;       // 题目测试点根目录
    std::string src_path;       // 待测源码文件路径
    std::string lang;           // 代码语言 (cpp/c/py...)
    int time_limit_ms = 1000;   // 默认时限 1000ms
    int memory_limit_mb = 256;  // 默认内存 256MB
    bool json_output = false;   // 是否输出为 JSON 格式
    bool fail_fast = true;      // 是否开启 ACM 赛制 (首错即止)
};
```

#### 评测机自身退出码规范 (Process Exit Code)：
不仅被测程序有退出码，`./core-judge` 这个工具本身也对宿主机操作系统承诺返回值：
- `0` (Success)：评测正常完成（无论学生代码是 AC、WA 还是 TLE，评测机本身工作正常）。
- `1` (CLI Error)：参数错误（如漏传必要参数，或者找不到提交的源码文件）。
- `2` (System Error)：系统环境故障（如沙箱权限不足、磁盘完全写满无法创建临时目录）。

---

## 6. CLI 命令行参数规格说明

评测引擎统一由 `main.cpp` 解析标准参数，支持终端人类可读模式与 `--json` 机器解析模式。

```bash
# 基本用法
./core-judge \
  --prob-dir ./data/problems/1001 \
  --src ./data/submissions/sol.cpp \
  --lang cpp \
  --time 1000 \
  --mem 262144 \
  --json
```

**JSON 机器标准输出 Schema：**
```json
{
  "verdict": "AC",
  "max_time_ms": 14,
  "peak_memory_kb": 2048,
  "compile_error": "",
  "test_cases": [
    { "id": 1, "verdict": "AC", "time_ms": 10, "memory_kb": 1800 },
    { "id": 2, "verdict": "AC", "time_ms": 14, "memory_kb": 2048 }
  ]
}
```

---

## 7. 红队安全与验收测试集 (Verification Matrix)

项目编码完成后，必须跑通以下特意构造的对抗性用例，全部正确识别方可进入发布阶段：

| 测试代码文件名 | 代码特征 | 预期断言结果 |
| :--- | :--- | :--- |
| `case_ac.cpp` | 标准 A+B 正常退出 | 必须判定为 **AC** |
| `case_wa.cpp` | 输出故意偏移 | 必须判定为 **WA** |
| `case_cpu_tle.cpp` | `while(1);` 纯死循环 | 必须在 1.x 秒内判定为 **TLE** (SIGXCPU) |
| `case_sleep_tle.cpp` | `sleep(100);` 伪装睡眠 | 必须由 Watchdog 强杀并判定为 **TLE** |
| `case_mle.cpp` | `while(1) malloc(1024*1024);` 频繁申请物理页 | 必须判定为 **MLE** |
| `case_ole.cpp` | 死循环向标准输出写入海量数据 | 必须判定为 **OLE** |
| `case_segfault.cpp` | `int* p = nullptr; *p = 1;` 非法内存访问 | 必须判定为 **RE** (SIGSEGV) |
| `case_divzero.cpp` | `int a = 1 / 0;` 浮点异常 | 必须判定为 **RE** (SIGFPE) |
| `case_forkbomb.cpp` | `while(1) fork();` 进程轰炸 | **不能影响系统**，必须由限制拦截并判 **RE** |

---

