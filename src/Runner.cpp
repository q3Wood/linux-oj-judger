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

    // 创建 POSIX 错误传递管道
    int errPipe[2];
    if (pipe(errPipe) < 0) {
        result.status = Status::SYSTEM_ERROR;
        result.error_msg = "Failed to create IPC pipe";
        return result;
    }

    // 设置写端为 FD_CLOEXEC：当 execv 成功时，内核会自动关闭该文件描述符！
    fcntl(errPipe[1], F_SETFD, FD_CLOEXEC);

    auto startTime = std::chrono::high_resolution_clock::now();

    pid_t pid = fork();

    if (pid < 0) {
        close(errPipe[0]);
        close(errPipe[1]);
        result.status = Status::SYSTEM_ERROR;
        result.error_msg = "Fork child process failed!";
        return result;
    }

    if (pid == 0) {
        // ================= 【子进程区间】 =================
        close(errPipe[0]); // 子进程关闭读端

        setpgid(0, 0);

        // 辅助 lambda 函数：一旦初始化失败，将 errno 写入管道并立刻退出
        auto check_and_report = [&](int ret) {
            if (ret != 0) {
                int err = errno;
                ssize_t w = write(errPipe[1], &err, sizeof(err));
                (void)w;
                _exit(1);
            }
        };

        // 1. 禁用 Core Dump，校验返回值
        struct rlimit core_limit;
        core_limit.rlim_cur = 0;
        core_limit.rlim_max = 0;
        check_and_report(setrlimit(RLIMIT_CORE, &core_limit));

        // 2. 限制输出文件大小 (RLIMIT_FSIZE)，校验返回值
        struct rlimit fsize_limit;
        fsize_limit.rlim_cur = (rlim_t)maxOutputSizeKb * 1024;
        fsize_limit.rlim_max = (rlim_t)maxOutputSizeKb * 1024;
        check_and_report(setrlimit(RLIMIT_FSIZE, &fsize_limit));

        // 3. 限制虚拟内存 (RLIMIT_AS)，校验返回值
        struct rlimit mem_limit;
        rlim_t bytes_limit = (rlim_t)(memoryLimitKb + 32 * 1024) * 1024;
        mem_limit.rlim_cur = bytes_limit;
        mem_limit.rlim_max = bytes_limit;
        check_and_report(setrlimit(RLIMIT_AS, &mem_limit));

        // 4. 限制 CPU 时间 (RLIMIT_CPU)，校验返回值
        struct rlimit cpu_limit;
        rlim_t cpu_sec = (rlim_t)((timeLimitMs + 999) / 1000 + 1);
        cpu_limit.rlim_cur = cpu_sec;
        cpu_limit.rlim_max = cpu_sec;
        check_and_report(setrlimit(RLIMIT_CPU, &cpu_limit));

        // 5. 适当放宽 RLIMIT_NPROC 至 256（减轻同一 UID 多线程并发挤占）
        struct rlimit nproc_limit;
        nproc_limit.rlim_cur = 256;
        nproc_limit.rlim_max = 256;
        check_and_report(setrlimit(RLIMIT_NPROC, &nproc_limit));

        // 6. 重定向输入输出 / 丢弃 stderr
        int inFd = open(inputPath.c_str(), O_RDONLY);
        int outFd = open(outputPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        int errFd = open("/dev/null", O_WRONLY);

        if (inFd < 0 || outFd < 0 || errFd < 0) {
            int err = errno;
            ssize_t w = write(errPipe[1], &err, sizeof(err));
            (void)w;
            _exit(1);
        }

        dup2(inFd, STDIN_FILENO);
        dup2(outFd, STDOUT_FILENO);
        dup2(errFd, STDERR_FILENO);

        close(inFd);
        close(outFd);
        close(errFd);

        // 加载用户代码
        char* args[] = { (char*)execPath.c_str(), NULL };
        execv(execPath.c_str(), args);

        // 如果走到这里，说明 execv 失败！写入 errno 并退出
        int err = errno;
        ssize_t w = write(errPipe[1], &err, sizeof(err));
        (void)w;
        _exit(1);
    } else {
        // ================= 【父进程区间】 =================
        close(errPipe[1]); // 父进程关闭写端

        int childErrno = 0;
        // 尝试从管道读取错误信息
        ssize_t n = read(errPipe[0], &childErrno, sizeof(childErrno));
        close(errPipe[0]); // 读完后关闭管道

        // 如果能读取到数据，说明子进程在 execv 之前或 execv 本身出错了！
        if (n > 0) {
            int status;
            waitpid(pid, &status, 0); // 回收子进程
            result.status = Status::SYSTEM_ERROR;
            result.error_msg = "Child process setup or execv failed with errno " + std::to_string(childErrno);
            return result;
        }

        // 如果读取到 0 字节 (EOF)，说明 execv 成功触发了 FD_CLOEXEC 自动关闭管道写端！
        // 子进程已安全加载用户代码，继续进入监控循环：
        int status = 0;
        int elapsedMs = 0;
        bool isTimeout = false;
        struct rusage child_usage;

        while (true) {
            pid_t res = wait4(pid, &status, WNOHANG, &child_usage);

            if (res == pid) {
                break; // 子进程终止
            } else if (res == 0) {
                usleep(10000); // 10ms
                elapsedMs += 10;

                if (elapsedMs >= timeLimitMs) {
                    kill(-pid, SIGKILL); // 强杀整个进程组
                    wait4(pid, &status, 0, &child_usage);
                    isTimeout = true;
                    break;
                }
            } else {
                if (errno == EINTR) continue; // 信号打断继续循环
                kill(-pid, SIGKILL);
                result.status = Status::SYSTEM_ERROR;
                result.error_msg = "wait4 failed with errno " + std::to_string(errno);
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

        // 2. 用户代码正常 exit
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                result.status = Status::RUNTIME_ERROR;
                return result;
            }
        }

        // 3. 用户代码被信号杀死（SIGSEGV, SIGKILL, SIGXFSZ 等）
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGXFSZ) {
                result.status = Status::OUTPUT_LIMIT_EXCEEDED;
            } else if (sig == SIGKILL) {
                if (result.memory_cost_kb >= memoryLimitKb) {
                    result.status = Status::MEMORY_LIMIT_EXCEEDED;
                } else {
                    result.status = Status::TIME_LIMIT_EXCEEDED;
                }
            } else if (sig == SIGXCPU) {
                result.status = Status::TIME_LIMIT_EXCEEDED;
            } else {
                result.status = Status::RUNTIME_ERROR;
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