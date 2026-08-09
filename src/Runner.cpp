#include "Runner.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
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
        // ================= 【子进程：内核级安全限制与隔离】 =================
        
        // 1. 文件大小限制 (RLIMIT_FSIZE) - 20MB，触发 SIGXFSZ
        struct rlimit fsize_limit;
        fsize_limit.rlim_cur = 20 * 1024 * 1024;
        fsize_limit.rlim_max = 20 * 1024 * 1024;
        setrlimit(RLIMIT_FSIZE, &fsize_limit);

        // 2. 内存限制 (RLIMIT_AS) - 动态使用 memoryLimitKb，额外给 32MB 作为动态链接库缓冲
        struct rlimit mem_limit;
        rlim_t bytes_limit = (rlim_t)(memoryLimitKb + 32 * 1024) * 1024;
        mem_limit.rlim_cur = bytes_limit;
        mem_limit.rlim_max = bytes_limit;
        setrlimit(RLIMIT_AS, &mem_limit);

        // 3. 限制 CPU 时间 (RLIMIT_CPU) - 作为内核最后兜底限制
        struct rlimit cpu_limit;
        rlim_t cpu_sec = (rlim_t)((timeLimitMs + 999) / 1000 + 1); // 向上取整加 1 秒
        cpu_limit.rlim_cur = cpu_sec;
        cpu_limit.rlim_max = cpu_sec;
        setrlimit(RLIMIT_CPU, &cpu_limit);

        // 4. 防 Fork 炸弹 - 限制该用户最大线程/进程数
        struct rlimit nproc_limit;
        nproc_limit.rlim_cur = 1;
        nproc_limit.rlim_max = 1;
        setrlimit(RLIMIT_NPROC, &nproc_limit);

        // 5. 重定向 stdin / stdout / stderr (丢弃用户程序的 stderr，防止污染终端)
        int inFd = open(inputPath.c_str(), O_RDONLY);
        int outFd = open(outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        int errFd = open("/dev/null", O_WRONLY);

        if (inFd < 0 || outFd < 0 || errFd < 0) {
            exit(1);
        }

        dup2(inFd, STDIN_FILENO);
        dup2(outFd, STDOUT_FILENO);
        dup2(errFd, STDERR_FILENO);

        close(inFd);
        close(outFd);
        close(errFd);

        char* args[] = { (char*)execPath.c_str(), NULL };
        execv(execPath.c_str(), args);
        exit(1); // 如果 execv 失败
    } else {
        // ================= 【父进程：精准统计与状态映射】 =================
        int status = 0;
        int elapsedMs = 0;
        bool isTimeout = false;
        struct rusage child_usage;

        while (true) {
            // 使用 wait4 代替 waitpid，直接拿到当前子进程精确的 rusage！(修 Bug 2)
            pid_t res = wait4(pid, &status, WNOHANG, &child_usage);

            if (res == pid) {
                break; // 子进程终止
            } else if (res == 0) {
                usleep(10000); // 10ms
                elapsedMs += 10;

                if (elapsedMs >= timeLimitMs) {
                    kill(pid, SIGKILL); // 强杀
                    wait4(pid, &status, 0, &child_usage); // 回收并获取最终 rusage
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

        // 精确获取该子进程的独立物理内存峰值 (ru_maxrss 在 Linux 下为 KB)
        result.memory_cost_kb = static_cast<int>(child_usage.ru_maxrss);

        // 1. TLE 判定
        if (isTimeout) {
            result.status = Status::TIME_LIMIT_EXCEEDED;
            return result;
        }

        // 2. 状态与信号精准解析 (修 Bug 1 & 4)
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGXFSZ) {
                result.status = Status::OUTPUT_LIMIT_EXCEEDED; // 正确判定 OLE
            } else if (sig == SIGKILL || sig == SIGXCPU) {
                result.status = Status::TIME_LIMIT_EXCEEDED;
            } else {
                result.status = Status::RUNTIME_ERROR; // SIGSEGV / SIGFPE / SIGABRT 等均判定 RE
            }
            return result;
        }

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                result.status = Status::RUNTIME_ERROR;
                return result;
            }
        }

        // 3. MLE 判别
        if (result.memory_cost_kb > memoryLimitKb) {
            result.status = Status::MEMORY_LIMIT_EXCEEDED;
            return result;
        }
    }

    return result;
}