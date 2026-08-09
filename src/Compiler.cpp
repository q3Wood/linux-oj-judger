#include "Compiler.hpp"
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>

bool Compiler::compileCpp(const std::string& srcPath, const std::string& execPath, std::string& errorMsg) {
    // 为编译日志赋予唯一文件名，避免并发覆盖
    std::string logPath = execPath + "_compile.log";

    pid_t pid = fork();
    if (pid < 0) {
        errorMsg = "Fork failed during compilation";
        return false;
    }

    if (pid == 0) {
        // 重定向 stderr 到日志文件
        int logFd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
        if (logFd >= 0) {
            dup2(logFd, STDERR_FILENO);
            close(logFd);
        }

        // 安全调用 g++，杜绝 shell 注入与空格路径错误
        char* args[] = {
            (char*)"g++",
            (char*)"-O2",
            (char*)srcPath.c_str(),
            (char*)"-o",
            (char*)execPath.c_str(),
            NULL
        };

        execvp("g++", args);
        exit(1);
    } else {
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            unlink(logPath.c_str()); // 编译成功，删除日志
            return true;
        } else {
            // 编译失败，读取日志
            std::ifstream logFile(logPath);
            std::stringstream ss;
            ss << logFile.rdbuf();
            errorMsg = ss.str();
            unlink(logPath.c_str());
            return false;
        }
    }
}