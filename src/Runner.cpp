#include "Runner.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <chrono>
#include <iostream>
#include <signal.h>

JudgeResult Runner::run(const std::string& execPath, 
                      const std::string& inputPath, 
                      const std::string& outputPath, 
                      int timeLimitMs) {
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
        // === 子进程：负责运行用户代码 ===
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
        // === 父进程：非阻塞轮询监控子进程是否超时 ===
        int status;
        int elapsedMs = 0;
        bool isTimeout = false;

        while (true) {
            // WNOHANG 选项表示不卡死等待，如果子进程没结束立刻返回 0
            pid_t res = waitpid(pid, &status, WNOHANG);

            if (res == pid) {
                // 子进程正常退出了！
                break;
            } else if (res == 0) {
                // 子进程还在运行，休眠 10 毫秒后重新检查
                usleep(10000); 
                elapsedMs += 10;

                // 判断是否超时！
                if (elapsedMs >= timeLimitMs) {
                    // 超时！给子进程发送 SIGKILL 强制杀绝信号！
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0); // 回收僵尸进程
                    isTimeout = true;
                    break;
                }
            } else {
                // 系统异常
                break;
            }
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
        result.time_cost_ms = static_cast<int>(duration);

        if (isTimeout) {
            result.status = Status::TIME_LIMIT_EXCEEDED;
            return result;
        }

        // 检查非超时的异常退出（如段错误、除以 0）
        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) != 0) {
                result.status = Status::RUNTIME_ERROR;
            }
        } else {
            result.status = Status::RUNTIME_ERROR;
        }
    }

    return result;
}