#ifndef RUNNER_HPP
#define RUNNER_HPP

#include <string>
#include "Result.hpp"

class Runner {
public:
    // 运行可执行文件，重定向输入输出，并统计运行毫秒数
    static JudgeResult run(const std::string& execPath, const std::string& inputPath, const std::string& outputPath, int timeLimitMs);
};

#endif