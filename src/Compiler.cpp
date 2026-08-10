#include "Compiler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <signal.h>

bool Compiler::compileCpp(const std::string& srcPath, const std::string& execPath, std::string& errorMsg) {
    std::string logPath = execPath + "_compile.log";

    pid_t pid = fork();
    if (pid < 0) {
        errorMsg = "Fork failed during compilation";
        return false;
    }

    if (pid == 0) {
        setpgid(0, 0);

        // 编译期防死锁：15 秒 CPU 时间，1GB 内存上限
        struct rlimit cpu_limit;
        cpu_limit.rlim_cur = 15;
        cpu_limit.rlim_max = 15;
        setrlimit(RLIMIT_CPU, &cpu_limit);

        struct rlimit mem_limit;
        rlim_t bytes_limit = (rlim_t)1024 * 1024 * 1024; // 1024 MB (1GB)
        mem_limit.rlim_cur = bytes_limit;
        mem_limit.rlim_max = bytes_limit;
        setrlimit(RLIMIT_AS, &mem_limit);

        int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (logFd >= 0) {
            // 同时重定向 stdout 和 stderr，彻底防止警告日志混入判题机的 JSON 流！
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            close(logFd);
        }

        char* args[] = {
            (char*)"g++",
            (char*)"-O2",
            (char*)srcPath.c_str(),
            (char*)"-o",
            (char*)execPath.c_str(),
            NULL
        };

        execvp("g++", args);
        _exit(1);
    } else {
        int status;
        int elapsedMs = 0;
        bool isTimeout = false;

        // 父进程防编译卡死循环
        while (true) {
            pid_t res = waitpid(pid, &status, WNOHANG);
            if (res == pid) break;
            if (res == 0) {
                usleep(20000); // 20ms
                elapsedMs += 20;
                if (elapsedMs >= 15000) { // 15秒超时
                    kill(-pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    isTimeout = true;
                    break;
                }
            } else {
                break;
            }
        }

        if (isTimeout) {
            errorMsg = "Compilation Timed Out (Exceeded 15 seconds)";
            unlink(logPath.c_str());
            return false;
        }

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            unlink(logPath.c_str()); // 编译成功删除日志
            return true;
        } else {
            std::ifstream logFile(logPath);
            std::stringstream ss;
            ss << logFile.rdbuf();
            errorMsg = ss.str();
            unlink(logPath.c_str());
            return false;
        }
    }
}