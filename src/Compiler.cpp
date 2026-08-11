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
        auto check_and_report = [](int ret) {
            if (ret != 0) _exit(1);
        };

        check_and_report(setpgid(0, 0));

        struct rlimit cpu_limit;
        cpu_limit.rlim_cur = 15;
        cpu_limit.rlim_max = 15;
        check_and_report(setrlimit(RLIMIT_CPU, &cpu_limit));

        struct rlimit mem_limit;
        rlim_t bytes_limit = (rlim_t)1024 * 1024 * 1024;
        mem_limit.rlim_cur = bytes_limit;
        mem_limit.rlim_max = bytes_limit;
        check_and_report(setrlimit(RLIMIT_AS, &mem_limit));

        int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (logFd >= 0) {
            dup2(logFd, STDOUT_FILENO);
            dup2(logFd, STDERR_FILENO);
            close(logFd);
        } else {
            int devNull = open("/dev/null", O_WRONLY);
            if (devNull >= 0) {
                dup2(devNull, STDOUT_FILENO);
                dup2(devNull, STDERR_FILENO);
                close(devNull);
            }
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

        while (true) {
            pid_t res = waitpid(pid, &status, WNOHANG);
            if (res == pid) break;
            if (res == 0) {
                usleep(20000); // 20ms
                elapsedMs += 20;
                if (elapsedMs >= 15000) {
                    kill(-pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    isTimeout = true;
                    break;
                }
            } else {
                if (errno == EINTR) continue;
                break;
            }
        }

        if (isTimeout) {
            errorMsg = "Compilation Timed Out (Exceeded 15 seconds limit)";
            unlink(logPath.c_str());
            return false;
        }

        if (WIFEXITED(status)) {
            if (WEXITSTATUS(status) == 0) {
                unlink(logPath.c_str());
                return true;
            } else {
                std::ifstream logFile(logPath);
                std::stringstream ss;
                ss << logFile.rdbuf();
                errorMsg = ss.str();
                if (errorMsg.empty()) {
                    errorMsg = "Compilation failed with exit code " + std::to_string(WEXITSTATUS(status));
                }
                unlink(logPath.c_str());
                return false;
            }
        } else if (WIFSIGNALED(status)) {
            errorMsg = "Compiler terminated by signal " + std::to_string(WTERMSIG(status));
            unlink(logPath.c_str());
            return false;
        }

        errorMsg = "Unknown compilation error";
        unlink(logPath.c_str());
        return false;
    }
}