#include "Runner.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <chrono>
#include <iostream>
#include <signal.h>
#include <cerrno>
#include <poll.h> // 引入 poll API 防范管道死锁

JudgeResult Runner::run(const std::string& execPath, 
                      const std::string& inputPath, 
                      const std::string& outputPath, 
                      int timeLimitMs,
                      int memoryLimitKb,
                      int maxOutputSizeKb) {
    JudgeResult result;
    result.status = Status::ACCEPTED;

    int errPipe[2];
    if (pipe(errPipe) < 0) {
        result.status = Status::SYSTEM_ERROR;
        result.error_msg = "Failed to create IPC pipe";
        return result;
    }

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
        close(errPipe[0]);

        auto check_and_report = [&](int ret) {
            if (ret != 0) {
                int err = errno;
                ssize_t w = write(errPipe[1], &err, sizeof(err));
                (void)w;
                _exit(1);
            }
        };

        // 校验 setpgid 和所有 setrlimit 返回值
        check_and_report(setpgid(0, 0));

        struct rlimit core_limit;
        core_limit.rlim_cur = 0;
        core_limit.rlim_max = 0;
        check_and_report(setrlimit(RLIMIT_CORE, &core_limit));

        struct rlimit fsize_limit;
        fsize_limit.rlim_cur = (rlim_t)maxOutputSizeKb * 1024;
        fsize_limit.rlim_max = (rlim_t)maxOutputSizeKb * 1024;
        check_and_report(setrlimit(RLIMIT_FSIZE, &fsize_limit));

        struct rlimit mem_limit;
        rlim_t bytes_limit = (rlim_t)(memoryLimitKb + 32 * 1024) * 1024;
        mem_limit.rlim_cur = bytes_limit;
        mem_limit.rlim_max = bytes_limit;
        check_and_report(setrlimit(RLIMIT_AS, &mem_limit));

        struct rlimit cpu_limit;
        rlim_t cpu_sec = (rlim_t)((timeLimitMs + 999) / 1000 + 1);
        cpu_limit.rlim_cur = cpu_sec;
        cpu_limit.rlim_max = cpu_sec;
        check_and_report(setrlimit(RLIMIT_CPU, &cpu_limit));

        struct rlimit nproc_limit;
        nproc_limit.rlim_cur = 256;
        nproc_limit.rlim_max = 256;
        check_and_report(setrlimit(RLIMIT_NPROC, &nproc_limit));

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

        char* args[] = { (char*)execPath.c_str(), NULL };
        execv(execPath.c_str(), args);

        int err = errno;
        ssize_t w = write(errPipe[1], &err, sizeof(err));
        (void)w;
        _exit(1);
    } else {
        // ================= 【父进程区间】 =================
        close(errPipe[1]);

        int childErrno = 0;
        ssize_t n = 0;

        // 使用 poll 监听管道读端，设置 3 秒超时防范子进程死锁
        struct pollfd pfd;
        pfd.fd = errPipe[0];
        pfd.events = POLLIN;

        int pollRes = poll(&pfd, 1, 3000); // 3000ms 超时
        if (pollRes > 0) {
            // 安全读取并处理 EINTR 信号打断
            while ((n = read(errPipe[0], &childErrno, sizeof(childErrno))) < 0) {
                if (errno == EINTR) continue;
                break;
            }
        } else if (pollRes == 0) {
            // 管道读取 3 秒超时，强杀子进程
            kill(-pid, SIGKILL);
            int status;
            waitpid(pid, &status, 0);
            close(errPipe[0]);
            result.status = Status::SYSTEM_ERROR;
            result.error_msg = "Child process setup timed out in pipe read";
            return result;
        }
        close(errPipe[0]);

        if (n > 0) {
            int status;
            waitpid(pid, &status, 0);
            result.status = Status::SYSTEM_ERROR;
            result.error_msg = "Child process setup/execv failed with errno " + std::to_string(childErrno);
            return result;
        }

        int status = 0;
        int elapsedMs = 0;
        bool isTimeout = false;
        struct rusage child_usage;

        while (true) {
            pid_t res = wait4(pid, &status, WNOHANG, &child_usage);

            if (res == pid) {
                break;
            } else if (res == 0) {
                usleep(10000); // 10ms
                elapsedMs += 10;

                if (elapsedMs >= timeLimitMs) {
                    kill(-pid, SIGKILL);
                    wait4(pid, &status, 0, &child_usage);
                    isTimeout = true;
                    break;
                }
            } else {
                if (errno == EINTR) continue;
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
            result.error_msg = "Time Limit Exceeded (" + std::to_string(timeLimitMs) + " ms)";
            return result;
        }

        // 2. 正常 Exit
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                result.status = Status::RUNTIME_ERROR;
                result.error_msg = "Non-zero exit code: " + std::to_string(WEXITSTATUS(status));
                return result;
            }
        }

        // 3. 信号处理与归因补充
        if (WIFSIGNALED(status)) {
            int sig = WTERMSIG(status);
            if (sig == SIGXFSZ) {
                result.status = Status::OUTPUT_LIMIT_EXCEEDED;
                result.error_msg = "Output Limit Exceeded (Exceeded " + std::to_string(maxOutputSizeKb) + " KB)";
            } else if (sig == SIGKILL) {
                if (result.memory_cost_kb >= memoryLimitKb) {
                    result.status = Status::MEMORY_LIMIT_EXCEEDED;
                    result.error_msg = "Memory Limit Exceeded (OOM Killed, Used: " + std::to_string(result.memory_cost_kb) + " KB)";
                } else {
                    result.status = Status::TIME_LIMIT_EXCEEDED;
                    result.error_msg = "Killed by SIGKILL";
                }
            } else if (sig == SIGXCPU) {
                result.status = Status::TIME_LIMIT_EXCEEDED;
                result.error_msg = "CPU Time Limit Exceeded";
            } else {
                result.status = Status::RUNTIME_ERROR;
                result.error_msg = "Terminated by signal " + std::to_string(sig);
            }
            return result;
        }

        // 4. MLE 判别与消息填充
        if (result.memory_cost_kb > memoryLimitKb) {
            result.status = Status::MEMORY_LIMIT_EXCEEDED;
            result.error_msg = "Memory Limit Exceeded (Used: " + std::to_string(result.memory_cost_kb) + " KB, Limit: " + std::to_string(memoryLimitKb) + " KB)";
            return result;
        }
    }

    return result;
}