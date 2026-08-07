# Linux C++ OJ Judger Engine (测评机引擎)

一个基于 Linux 系统调用的轻量级 C++ 在线评测（Online Judge）执行与判题引擎。

## 🌟 核心特性

- **多进程隔离运行**：基于 `fork` + `execv` 在独立子进程中运行用户代码。
- **输入输出重定向**：使用 POSIX `dup2` 系统调用将文件完美重定向至标准输入输出 (`cin`/`cout`)。
- **高精度耗时统计**：采用 C++11 `<chrono>` 库精确统计毫秒级执行时间。
- **超时强杀 (TLE) 机制**：采用非阻塞轮询 (`WNOHANG`) 结合 `SIGKILL` 信号，防止死循环卡死服务器。
- **错误状态捕获**：准确识别 `Accepted`、`Runtime Error (RE)`、`Time Limit Exceeded (TLE)` 等运行状态。
- **智能结果比对**：支持过滤末尾空格和换行符的 Token 级内容比对 (Comparer)。

## 🛠️ 环境要求

- **操作系统**：Linux (Ubuntu / CentOS / Debian 等)
- **编译器**：GCC/G++ 7.0+ (支持 C++11/C++17)
- **构建工具**：CMake 3.10+ 或 Make

## 🚀 快速开始与编译

```bash
# 1. 克隆项目
git clone https://github.com/你的用户名/my-oj-judger.git
cd my-oj-judger

# 2. 编译项目 (使用 g++)
g++ -std=c++17 src/*.cpp -Iinclude -o judger

# 3. 运行测评机
./judger