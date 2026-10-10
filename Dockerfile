# 1. 基础镜像：使用纯净的 Ubuntu 22.04
FROM ubuntu:22.04

# 2. 避免安装过程中的交互提示
ENV DEBIAN_FRONTEND=noninteractive

# 3. 安装必要的编译运行环境 (C++17, CMake)
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    g++ \
    && rm -rf /var/lib/apt/lists/*

# 4. 设置工作目录并将工程拷贝进去
WORKDIR /app
COPY . /app

# 5. 编译构建 core-judge
RUN cmake -B build && cmake --build build

# 6. 默认入口
ENTRYPOINT ["/app/build/core-judge"]