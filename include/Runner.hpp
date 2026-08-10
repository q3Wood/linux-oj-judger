#ifndef RUNNER_HPP
#define RUNNER_HPP

#include <string>
#include "Result.hpp"

class Runner {
public:
    static JudgeResult run(const std::string& execPath, 
                          const std::string& inputPath, 
                          const std::string& outputPath, 
                          int timeLimitMs,
                          int memoryLimitKb,
                          int maxOutputSizeKb = 20 * 1024); // 默认 20MB
};

#endif