# core-judge

A high-performance, standalone Online Judge evaluation engine written in modern C++17, designed for secure, sub-millisecond competitive programming assessment.

---

## Features
- **POSIX Kernel Sandboxing**: Employs `setrlimit` (RLIMIT_CPU, RLIMIT_STACK, RLIMIT_FSIZE, RLIMIT_NPROC) and unprivileged user drop (`setuid(nobody)`).
- **Dual-Timer Watchdog**: Dedicated monitor thread ensuring zero-hang protection against malicious `sleep()` or I/O locks.
- **Microsecond Precision**: Accurate CPU user/kernel runtime and Resident Set Size (Peak RSS) measurement via `wait4()`.
- **Strict Stream Checker**: Efficient whitespace/newline-insensitive comparison.
- **Docker-Ready**: Full network isolation support (`--net=none`) and read-only volume mounting.
- **Machine & Human Interfaces**: Rich ANSI terminal color reporting + JSON payload contract for easy backend integration.

## Architecture

```text
[Task Request] -> [Compiler] -> [POSIX Sandbox Runner] -> [Diff Checker] -> [JSON Report]
                                          │
                                   [Watchdog Thread]
```

## Quick Start

### 1. Build from Source
```bash
cmake -B build
cmake --build build
```

### 2. Evaluate a Submission
```bash
./build/core-judge \
  --prob-dir data/problems/1001 \
  --src tests/fixtures/my_ac_solution.cpp \
  --time 1000 \
  --mem 256
```

### 3. Machine JSON Mode
```bash
./build/core-judge \
  --prob-dir data/problems/1001 \
  --src tests/fixtures/my_ac_solution.cpp \
  --json
```

## Docker Sandboxed Execution
```bash
docker build -t core-judge-image .
docker run --rm --net=none \
  -v $(pwd)/data:/app/data:ro \
  core-judge-image \
  --prob-dir /app/data/problems/1001 \
  --src /app/tests/fixtures/my_ac_solution.cpp
```

