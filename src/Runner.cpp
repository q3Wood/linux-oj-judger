#include "Runner.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <chrono>
#include <iostream>
#include <signal.h>
#include <cerrno>

JudgeResult Runner::run(const std::string& execPath, 
                      const std::string& inputPath, 
                      const std::string& outputPath, 
                      int timeLimitMs,
                      int memoryLimitKb,
                      int maxOutputSizeKb) {
    JudgeResult result;
    result.status = Status::ACCEPTED;

    auto startTime = std::chrono::high_resolution_clock::now();

    pid_t pid = fork();

    if (pid < 0) {
        result.status = Status::SYSTEM_ERROR;
        result.error_msg = "Fork child process failed!";
        return result;
    }

    if (pid == 0) {
        // ================= 【子进程配置】 =================
        // 1. 设置独立的进程组 ID，确保父进程能通过 kill(-pgid) 杀掉所有孙进程
        setpgid(0, 0);

        // 2. 禁用 Core Dump，防止生成巨大文件爆硬盘
        struct rlimit core_limit;
        core_limit.rlim_cur = 0;
        core_limit.rlim_max = 0;
        setrlimit(RLIMIT_CORE, &core_limit);

        // 3. 限制输出文件大小 (RLIMIT_FSIZE)
        struct rlimit fsize_limit;
        fsize_limit.rlim_cur = (rlim_t)maxOutputSizeKb * 1024;
        fsize_limit.rlim_max = (rlim_t)maxOutputSizeKb * 1024;
        setrlimit(RLIMIT_FSIZE, &fsize_limit);

        // 4. 限制虚拟内存 (RLIMIT_AS)
        struct rlimit mem_limit;
        rlim_t bytes_limit = (rlim_t)(memoryLimitKb + 32 * 1024) * 1024; // 给予 32MB 动态库缓冲
        mem_limit.rlim_cur = bytes_limit;
        mem_limit.rlim_max = bytes_limit;
        setrlimit(RLIMIT_AS, &mem_limit);

        // 5. 限制 CPU 时间 (RLIMIT_CPU)
        struct rlimit cpu_limit;
        rlim_t cpu_sec = (rlim_t)((timeLimitMs + 999) / 1000 + 1);
        cpu_limit.rlim_cur = cpu_sec;
        cpu_limit.rlim_max = cpu_sec;
        setrlimit(RLIMIT_CPU, &cpu_limit);

        // 6. 支持多线程：允许最多创建 64 个线程/子进程
        struct rlimit nproc_limit;
        nproc_limit.rlim_cur = 64;
        nproc_limit.rlim_max = 64;
        setrlimit(RLIMIT_NPROC, &nproc_limit);

        // 7. 重定向输入输出/丢弃 stderr
        int inFd = open(inputPath.c_str(), O_RDONLY);
        int outFd = open(outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        int errFd = open("/dev/null", O_WRONLY);

        if (inFd < 0 || outFd < 0 || errFd < 0) {
            _exit(222); // 专用错误码：系统打开文件失败
        }

        dup2(inFd, STDIN_FILENO);
        dup2(outFd, STDOUT_FILENO);
        dup2(errFd, STDERR_FILENO);

        close(inFd);
        close(outFd);
        close(errFd);

        char* args[] = { (char*)execPath.c_str(), NULL };
        execv(execPath.c_str(), args);

        _exit(222); // execv 执行失败
    } else {
        // ================= 【父进程监控】 =================
        int status = 0;
        int elapsedMs = 0;
        bool isTimeout = false;
        struct rusage child_usage;

        while (true) {
            pid_t res = wait4(pid, &status, WNOHANG, &child_usage);

            if (res == pid) {
                break; // 子进程正常退出或被终止
            } else if (res == 0) {
                usleep(10000); // 10ms
                elapsedMs += 10;

                if (elapsedMs >= timeLimitMs) {
                    // 超时：向整个进程组发送 SIGKILL，斩草除根（防止孤儿孙进程）
                    kill(-pid, SIGKILL);
                    wait4(pid, &status, 0, &child_usage);
                    isTimeout = true;
                    break;
                }
            } else {
                // wait4 返回 -1 的健壮性处理
                if (errno == EINTR) continue; // 被信号中断则继续循环
                kill(-pid, SIGKILL);
                result.status = Status::SYSTEM_ERROR;
                result.error_msg = "wait4 system call failed with errno " + std::to_string(errno);
                return result;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        result.time_cost_ms = static_cast<int>(duration);
        result.memory_cost_kb = static_cast<int>(child_usage.ru_maxrss);

        // 1. TLE 判别
        if (isTimeout) {
            result.status = Status::TIME_LIMIT_EXCEEDED;
            return result;
        }

        // 2. 检查专用系统异常退出码
        if (WIFEXITED(status)) {
            int exitCode = WEXITSTATUS(status);
            if (exitCode == 222) {
                result.status = Status::SYSTEM_ERROR;
                result.error_msg = "Child process execv or file open failed";
                return result;
            }
            if (exitCode != 0) {
                result.status = Status::RUNTIME_ERROR;
                return result;
            }
        }

        // 3. 信号处理与 OOM/OLE/TLE 归因区分
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGXFSZ) {
                result.status = Status::OUTPUT_LIMIT_EXCEEDED;
            } else if (sig == SIGKILL) {
                // 如果不是超时引起的 SIGKILL，但内存接近/超出限制，则归因于系统 OOM Killer
                if (result.memory_cost_kb >= memoryLimitKb) {
                    result.status = Status::MEMORY_LIMIT_EXCEEDED;
                } else {
                    result.status = Status::TIME_LIMIT_EXCEEDED;
                }
            } else if (sig == SIGXCPU) {
                result.status = Status::TIME_LIMIT_EXCEEDED;
            } else {
                result.status = Status::RUNTIME_ERROR; // SIGSEGV / SIGFPE / SIGABRT
            }
            return result;
        }

        // 4. MLE 事后检查
        if (result.memory_cost_kb > memoryLimitKb) {
            result.status = Status::MEMORY_LIMIT_EXCEEDED;
            return result;
        }
    }

    return result;
}