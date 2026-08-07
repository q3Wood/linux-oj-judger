#include <iostream>
#include <vector>
#include <algorithm>
#include "Compiler.hpp"
#include "Runner.hpp"
#include "Comparer.hpp"
#include "Result.hpp"

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "   JudgeCore (企业级判题引擎 v2.0)       " << std::endl;
    std::cout << "========================================" << std::endl;

    std::string srcPath = "../test_cases/solution.cpp";
    std::string execPath = "../workspace/solution";
    int timeLimitMs = 1000; // 设定时间限制为 1000ms (1秒)
    int caseCount = 2;       // 假设这道题有 2 组测试用例
    std::string errorMsg;

    // 1. 编译阶段
    std::cout << "[1] 正在编译源码..." << std::endl;
    if (!Compiler::compileCpp(srcPath, execPath, errorMsg)) {
        std::cout << "[结果]: CE (Compile Error)" << std::endl;
        std::cout << errorMsg << std::endl;
        return 0;
    }
    std::cout << "-> 编译成功！" << std::endl;

    // 2. 循环测试所有用例
    std::cout << "[2] 开始评测多组测试用例 (共 " << caseCount << " 组)..." << std::endl;
    
    int maxTimeCost = 0;

    for (int i = 1; i <= caseCount; ++i) {
        std::string inputPath = "../test_cases/" + std::to_string(i) + ".in";
        std::string stdOutPath = "../test_cases/" + std::to_string(i) + ".out";
        std::string userOutPath = "../workspace/user_" + std::to_string(i) + ".out";

        std::cout << "  -> 正在测试第 " << i << " 组数据... " << std::flush;

        // 运行代码 (带有 1000ms 强制超时限制)
        JudgeResult runResult = Runner::run(execPath, inputPath, userOutPath, timeLimitMs);

        // 检查运行状态
        if (runResult.status == Status::TIME_LIMIT_EXCEEDED) {
            std::cout << "失败！" << std::endl;
            std::cout << "[结果]: TLE (Time Limit Exceeded - 在第 " << i << " 组数据超时！)" << std::endl;
            return 0;
        }
        if (runResult.status == Status::RUNTIME_ERROR) {
            std::cout << "失败！" << std::endl;
            std::cout << "[结果]: RE (Runtime Error - 在第 " << i << " 组数据崩溃！)" << std::endl;
            return 0;
        }

        // 比对答案
        bool isMatched = Comparer::compare(userOutPath, stdOutPath);
        if (!isMatched) {
            std::cout << "失败！" << std::endl;
            std::cout << "[结果]: WA (Wrong Answer - 在第 " << i << " 组数据答案错误！)" << std::endl;
            return 0;
        }

        maxTimeCost = std::max(maxTimeCost, runResult.time_cost_ms);
        std::cout << "通过! (耗时: " << runResult.time_cost_ms << " ms)" << std::endl;
    }

    // 所有用例全部通过！
    std::cout << "========================================" << std::endl;
    std::cout << "[结果]: AC (Accepted) - 全部测试用例通过！" << std::endl;
    std::cout << "最大运行时间: " << maxTimeCost << " ms" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}