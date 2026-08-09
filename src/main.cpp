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
    std::string problemId = "1";
    std::string srcPath = "../test_cases/1/solution.cpp";
    int timeLimitMs = 1000;          
    int memoryLimitKb = 128 * 1024;   

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
    std::string workspaceDir = "../workspace";
    std::string execPath = workspaceDir + "/solution_" + problemId;
    std::string errorMsg;

    // 健壮性保障 1: 自动创建 workspace 目录
    try {
        fs::create_directories(workspaceDir);
    } catch (const std::exception& e) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, "Failed to create workspace directory", 0, 0);
        return 0;
    }

    // 健壮性保障 2: 检查用例目录
    if (!fs::exists(testCasesDir)) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, "Testcase directory not found: " + testCasesDir, 0, 0);
        return 0;
    }

    // 1. 安全编译
    if (!Compiler::compileCpp(srcPath, execPath, errorMsg)) {
        outputJson(Status::COMPILE_ERROR, 0, 0, errorMsg, 0, 0);
        return 0;
    }

    // 健壮性保障 3: 搜集实际存在的 .in 文件名并排序
    std::vector<std::string> inFiles;
    for (const auto& entry : fs::directory_iterator(testCasesDir)) {
        if (entry.path().extension() == ".in") {
            inFiles.push_back(entry.path().stem().string()); // 拿到如 "1", "2"
        }
    }
    std::sort(inFiles.begin(), inFiles.end()); // 字典序排序

    int totalCount = inFiles.size();
    if (totalCount == 0) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, "No .in testcases found in " + testCasesDir, 0, 0);
        return 0;
    }

    int maxTimeCost = 0;
    int maxMemoryCost = 0;

    // 2. 遍历实际匹配的用例文件
    for (int i = 0; i < totalCount; ++i) {
        std::string baseName = inFiles[i];
        std::string inputPath = testCasesDir + "/" + baseName + ".in";
        std::string stdOutPath = testCasesDir + "/" + baseName + ".out";
        std::string userOutPath = workspaceDir + "/user_" + problemId + "_" + baseName + ".out";

        if (!fs::exists(stdOutPath)) {
            outputJson(Status::SYSTEM_ERROR, 0, 0, "Missing corresponding .out file for " + baseName + ".in", i, totalCount);
            return 0;
        }

        JudgeResult runResult = Runner::run(execPath, inputPath, userOutPath, timeLimitMs, memoryLimitKb);

        if (runResult.status != Status::ACCEPTED) {
            outputJson(runResult.status, runResult.time_cost_ms, runResult.memory_cost_kb, runResult.error_msg, i, totalCount);
            return 0;
        }

        if (!Comparer::compare(userOutPath, stdOutPath)) {
            outputJson(Status::WRONG_ANSWER, runResult.time_cost_ms, runResult.memory_cost_kb, "Output mismatch on " + baseName + ".in", i, totalCount);
            return 0;
        }

        maxTimeCost = std::max(maxTimeCost, runResult.time_cost_ms);
        maxMemoryCost = std::max(maxMemoryCost, runResult.memory_cost_kb);
    }

    outputJson(Status::ACCEPTED, maxTimeCost, maxMemoryCost, "", totalCount, totalCount);

    return 0;
}