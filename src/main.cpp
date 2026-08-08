#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unistd.h>
#include <getopt.h>
#include "Compiler.hpp"
#include "Runner.hpp"
#include "Comparer.hpp"
#include "Result.hpp"

namespace fs = std::filesystem;

// 1. 辅助函数：将 Status 枚举转为标准的英文字符串
std::string statusToString(Status status) {
    switch (status) {
        case Status::ACCEPTED:               return "ACCEPTED";
        case Status::WRONG_ANSWER:           return "WRONG_ANSWER";
        case Status::COMPILE_ERROR:          return "COMPILE_ERROR";
        case Status::RUNTIME_ERROR:          return "RUNTIME_ERROR";
        case Status::TIME_LIMIT_EXCEEDED:    return "TIME_LIMIT_EXCEEDED";
        case Status::MEMORY_LIMIT_EXCEEDED:  return "MEMORY_LIMIT_EXCEEDED";
        case Status::OUTPUT_LIMIT_EXCEEDED:  return "OUTPUT_LIMIT_EXCEEDED";
        case Status::SYSTEM_ERROR:           return "SYSTEM_ERROR";
        default:                             return "UNKNOWN_ERROR";
    }
}

// 2. 辅助函数：转义字符串中的双引号和换行符，防止破坏 JSON 格式
std::string escapeJson(const std::string& input) {
    std::string output;
    for (char c : input) {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c == '\n') output += "\\n";
        else if (c == '\r') output += "\\r";
        else if (c == '\t') output += "\\t";
        else output += c;
    }
    return output;
}

// 3. 辅助函数：统一在控制台最后打印标准的 JSON 结果字符串
void outputJson(Status status, int timeMs, int memoryKb, const std::string& errorMsg, int passCount, int totalCount) {
    std::cout << "[JSON_RESULT]:"
              << "{\"status\":\"" << statusToString(status) << "\","
              << "\"time_cost_ms\":" << timeMs << ","
              << "\"memory_cost_kb\":" << memoryKb << ","
              << "\"error_msg\":\"" << escapeJson(errorMsg) << "\","
              << "\"pass_count\":" << passCount << ","
              << "\"total_count\":" << totalCount << "}"
              << std::endl;
}

int main(int argc, char* argv[]) {
    // 默认参数
    std::string problemId = "1";
    std::string srcPath = "../test_cases/1/solution.cpp";
    int timeLimitMs = 1000;          
    int memoryLimitKb = 128 * 1024;   

    // 解析命令行参数
    int opt;
    while ((opt = getopt(argc, argv, "p:s:t:m:")) != -1) {
        switch (opt) {
            case 'p': problemId = optarg; break;
            case 's': srcPath = optarg; break;
            case 't': timeLimitMs = std::stoi(optarg); break;
            case 'm': memoryLimitKb = std::stoi(optarg); break;
            default: break;
        }
    }

    std::string testCasesDir = "../test_cases/" + problemId;
    std::string execPath = "../workspace/solution_" + problemId;
    std::string errorMsg;

    // 检查用例目录是否存在
    if (!fs::exists(testCasesDir)) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, "Testcase directory not found for problem " + problemId, 0, 0);
        return 0;
    }

    // 1. 编译阶段
    if (!Compiler::compileCpp(srcPath, execPath, errorMsg)) {
        outputJson(Status::COMPILE_ERROR, 0, 0, errorMsg, 0, 0);
        return 0;
    }

    // 2. 自动扫描用例目录
    int totalCount = 0;
    for (const auto& entry : fs::directory_iterator(testCasesDir)) {
        if (entry.path().extension() == ".in") {
            totalCount++;
        }
    }

    if (totalCount == 0) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, "No .in testcases found in " + testCasesDir, 0, 0);
        return 0;
    }

    int maxTimeCost = 0;
    int maxMemoryCost = 0;

    // 3. 循环测试所有用例
    for (int i = 1; i <= totalCount; ++i) {
        std::string inputPath = testCasesDir + "/" + std::to_string(i) + ".in";
        std::string stdOutPath = testCasesDir + "/" + std::to_string(i) + ".out";
        std::string userOutPath = "../workspace/user_" + problemId + "_" + std::to_string(i) + ".out";

        JudgeResult runResult = Runner::run(execPath, inputPath, userOutPath, timeLimitMs, memoryLimitKb);

        // 如果运行出错，提前返回对应错误的 JSON
        if (runResult.status != Status::ACCEPTED) {
            outputJson(runResult.status, runResult.time_cost_ms, runResult.memory_cost_kb, runResult.error_msg, i - 1, totalCount);
            return 0;
        }

        // 比对输出
        if (!Comparer::compare(userOutPath, stdOutPath)) {
            outputJson(Status::WRONG_ANSWER, runResult.time_cost_ms, runResult.memory_cost_kb, "Output mismatch on testcase " + std::to_string(i), i - 1, totalCount);
            return 0;
        }

        maxTimeCost = std::max(maxTimeCost, runResult.time_cost_ms);
        maxMemoryCost = std::max(maxMemoryCost, runResult.memory_cost_kb);
    }

    // 4. 全部测试点通过！输出最终成功的 JSON
    outputJson(Status::ACCEPTED, maxTimeCost, maxMemoryCost, "", totalCount, totalCount);

    return 0;
}