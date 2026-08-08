#ifndef RUNNER_HPP
#define RUNNER_HPP

#include <string>
#include "Result.hpp"

class Runner {
public:
    // 增加了 memoryLimitKb 参数 (内存限制，单位 KB，比如 128MB = 131072 KB)
    static JudgeResult run(const std::string& execPath, 
                          const std::string& inputPath, 
                          const std::string& outputPath, 
                          int timeLimitMs,
                          int memoryLimitKb);
};

#endif