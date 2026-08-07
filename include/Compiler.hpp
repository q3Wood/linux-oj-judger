#ifndef COMPILER_HPP
#define COMPILER_HPP

#include <string>

class Compiler {
public:
    // 编译指定的源码文件，成功返回 true，失败返回 false 并将错误日志写入 errorMsg
    static bool compileCpp(const std::string& srcPath, const std::string& execPath, std::string& errorMsg);
};

#endif