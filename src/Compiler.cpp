#include "Compiler.hpp"
#include <cstdlib>
#include <fstream>
#include <sstream>

bool Compiler::compileCpp(const std::string& srcPath, const std::string& execPath, std::string& errorMsg) {
    std::string logPath = "compile.log";
    // 拼接 g++ 编译命令
    std::string cmd = "g++ " + srcPath + " -o " + execPath + " 2> " + logPath;

    int status = system(cmd.c_str());

    if (status != 0) {
        // 编译失败，读取 compile.log 日志内容
        std::ifstream logFile(logPath);
        std::stringstream ss;
        ss << logFile.rdbuf();
        errorMsg = ss.str();
        return false;
    }
    return true;
}