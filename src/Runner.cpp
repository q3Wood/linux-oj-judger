#include "Runner.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h> // 引入资源限制和内存测量 API
#include <fcntl.h>
#include <chrono>
#include <iostream>
#include <signal.h>

JudgeResult Runner::run(const std::string& execPath, 
                      const std::string& inputPath, 
                      const std::string& outputPath, 
                      int timeLimitMs,
                      int memoryLimitKb) {
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
        // ================= 【子进程：安全防护配置】 =================
        
        // 1. 限制输出文件最大为 20MB (防止写爆硬盘)
        struct rlimit fsize_limit;
        fsize_limit.rlim_cur = 20 * 1024 * 1024; // 20 MB
        fsize_limit.rlim_max = 20 * 1024 * 1024;
        setrlimit(RLIMIT_FSIZE, &fsize_limit);

        // 2. 限制栈内存空间最大为 16MB (防止递归爆栈)
        struct rlimit stack_limit;
        stack_limit.rlim_cur = 16 * 1024 * 1024; // 16 MB
        stack_limit.rlim_max = 16 * 1024 * 1024;
        setrlimit(RLIMIT_STACK, &stack_limit);

        // 重定向文件
        int inFd = open(inputPath.c_str(), O_RDONLY);
        int outFd = open(outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);

        if (inFd < 0 || outFd < 0) {
            exit(1);
        }

        dup2(inFd, STDIN_FILENO);
        dup2(outFd, STDOUT_FILENO);
        close(inFd);
        close(outFd);

        char* args[] = { (char*)execPath.c_str(), NULL };
        execv(execPath.c_str(), args);
        exit(1);
    } else {
        // ================= 【父进程：监控与内存统计】 =================
        int status;
        int elapsedMs = 0;
        bool isTimeout = false;

        while (true) {
            pid_t res = waitpid(pid, &status, WNOHANG);

            if (res == pid) {
                break; // 子进程结束
            } else if (res == 0) {
                usleep(10000); // 休眠 10ms
                elapsedMs += 10;

                if (elapsedMs >= timeLimitMs) {
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    isTimeout = true;
                    break;
                }
            } else {
                break;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        result.time_cost_ms = static_cast<int>(duration);

        // 测量子进程消耗的【物理内存峰值】 (ru_maxrss 单位在 Linux 下是 KB)
        struct rusage r_usage;
        getrusage(RUSAGE_CHILDREN, &r_usage);
        result.memory_cost_kb = static_cast<int>(r_usage.ru_maxrss);

        // 1. 先检查是否超时 (TLE)
        if (isTimeout) {
            result.status = Status::TIME_LIMIT_EXCEEDED;
            return result;
        }

        // 2. 再检查内存是否爆了 (MLE)
        if (result.memory_cost_kb > memoryLimitKb) {
            result.status = Status::MEMORY_LIMIT_EXCEEDED;
            return result;
        }

        // 3. 检查异常崩溃 (RE)
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                result.status = Status::RUNTIME_ERROR;
            }
        } else if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGSEGV) {
                result.status = Status::OUTPUT_LIMIT_EXCEEDED; // 内存访问违规，可能是内存越界
            } else {
                result.status = Status::RUNTIME_ERROR; // 其他信号导致的崩溃
            }
        }
    }

    return result;
}