#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unistd.h> // 引入 POSIX 命令行参数解析库 getopt
#include "Compiler.hpp"
#include "Runner.hpp"
#include "Comparer.hpp"
#include "Result.hpp"

namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    // 1. 设置默认参数 (如果不传命令行参数，默认使用这些参数，方便本地测试)
    std::string problemId = "1";
    std::string srcPath = "../test_cases/1/solution.cpp";
    int timeLimitMs = 1000;          // 默认 1000ms
    int memoryLimitKb = 128 * 1024;   // 默认 128MB (131072 KB)

    // 2. 解析命令行参数: -p 题目ID -s 源码路径 -t 时间限制 -m 内存限制
    int opt;
    while ((opt = getopt(argc, argv, "p:s:t:m:")) != -1) {
        switch (opt) {
            case 'p':
                problemId = optarg;
                break;
            case 's':
                srcPath = optarg;
                break;
            case 't':
                timeLimitMs = std::stoi(optarg);
                break;
            case 'm':
                memoryLimitKb = std::stoi(optarg);
                break;
            default:
                std::cerr << "用法: " << argv[0] << " -p <题目ID> -s <源码路径> -t <时间限制ms> -m <内存限制KB>" << std::endl;
                return 1;
        }
    }

    std::string testCasesDir = "../test_cases/" + problemId;
    std::string execPath = "../workspace/solution_" + problemId;
    std::string errorMsg;

    std::cout << "========================================" << std::endl;
    std::cout << "   JudgeCore (企业级 CLI 判题引擎)       " << std::endl;
    std::cout << "   题目ID: " << problemId << " | 时限: " << timeLimitMs << "ms | 内存: " << (memoryLimitKb / 1024) << "MB" << std::endl;
    std::cout << "========================================" << std::endl;

    // 3. 检查用例目录是否存在
    if (!fs::exists(testCasesDir)) {
        std::cout << "[结果]: SE (System Error - 未找到题目 " << problemId << " 的测试用例目录！)" << std::endl;
        return 0;
    }

    // 4. 编译阶段
    std::cout << "[1] 正在编译源码: " << srcPath << " ..." << std::endl;
    if (!Compiler::compileCpp(srcPath, execPath, errorMsg)) {
        std::cout << "[结果]: CE (Compile Error)" << std::endl;
        std::cout << errorMsg << std::endl;
        return 0;
    }
    std::cout << "-> 编译成功！" << std::endl;

    // 5. 自动扫描用例
    int caseCount = 0;
    for (const auto& entry : fs::directory_iterator(testCasesDir)) {
        if (entry.path().extension() == ".in") {
            caseCount++;
        }
    }

    std::cout << "[2] 自动检索用例目录 [" << testCasesDir << "]，找到 " << caseCount << " 组测试用例..." << std::endl;

    int maxTimeCost = 0;
    int maxMemoryCost = 0;

    // 6. 循环评测每组用例
    for (int i = 1; i <= caseCount; ++i) {
        std::string inputPath = testCasesDir + "/" + std::to_string(i) + ".in";
        std::string stdOutPath = testCasesDir + "/" + std::to_string(i) + ".out";
        std::string userOutPath = "../workspace/user_" + problemId + "_" + std::to_string(i) + ".out";

        std::cout << "  -> 评测第 " << i << " 组用例... " << std::flush;

        JudgeResult runResult = Runner::run(execPath, inputPath, userOutPath, timeLimitMs, memoryLimitKb);

        if (runResult.status == Status::TIME_LIMIT_EXCEEDED) {
            std::cout << "失败！\n[结果]: TLE (Time Limit Exceeded - 第 " << i << " 组数据超时)" << std::endl;
            return 0;
        }
        if (runResult.status == Status::MEMORY_LIMIT_EXCEEDED) {
            std::cout << "失败！\n[结果]: MLE (Memory Limit Exceeded - 第 " << i << " 组数据内存超出限制！)" << std::endl;
            return 0;
        }
        if (runResult.status == Status::RUNTIME_ERROR) {
            std::cout << "失败！\n[结果]: RE (Runtime Error - 第 " << i << " 组数据运行崩溃)" << std::endl;
            return 0;
        }

        if (!Comparer::compare(userOutPath, stdOutPath)) {
            std::cout << "失败！\n[结果]: WA (Wrong Answer - 第 " << i << " 组数据答案错误)" << std::endl;
            return 0;
        }

        maxTimeCost = std::max(maxTimeCost, runResult.time_cost_ms);
        maxMemoryCost = std::max(maxMemoryCost, runResult.memory_cost_kb);

        std::cout << "通过! (耗时: " << runResult.time_cost_ms << " ms | 内存: " 
                  << runResult.memory_cost_kb << " KB)" << std::endl;
    }

    // 全部通过
    std::cout << "========================================" << std::endl;
    std::cout << "[结果]: AC (Accepted) - 所有 " << caseCount << " 组测试用例全部通过！" << std::endl;
    std::cout << "最大运行时间: " << maxTimeCost << " ms" << std::endl;
    std::cout << "最大消耗内存: " << maxMemoryCost << " KB" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}