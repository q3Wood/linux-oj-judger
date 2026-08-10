#include <iostream>
#include <vector>
#include <algorithm>
#include <filesystem>
#include <unistd.h>
#include <getopt.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
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

// 完善的 JSON 字符串转义（彻底转义 ASCII 0x00 - 0x1F 控制字符）
std::string escapeJson(const std::string& input) {
    std::ostringstream ss;
    for (unsigned char c : input) {
        switch (c) {
            case '"':  ss << "\\\""; break;
            case '\\': ss << "\\\\"; break;
            case '\b': ss << "\\b"; break;
            case '\f': ss << "\\f"; break;
            case '\n': ss << "\\n"; break;
            case '\r': ss << "\\r"; break;
            case '\t': ss << "\\t"; break;
            default:
                if (c < 0x20) {
                    ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    ss << c;
                }
                break;
        }
    }
    return ss.str();
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

// 自然数逻辑比较：确保 "2.in" 排在 "10.in" 前面
// 智能自然数排序：防止 "12" 和 "12b" 解析成相同数字导致排序不稳定
bool naturalSortCompare(const std::string& a, const std::string& b) {
    try {
        size_t idxA = 0, idxB = 0;
        int numA = std::stoi(a, &idxA);
        int numB = std::stoi(b, &idxB);

        // 如果数字前缀不相等，直接按数字大小排
        if (numA != numB) {
            return numA < numB;
        }
        // 如果数字前缀相同（例如 "12" 和 "12b"），则退回字符串字典序全量比对
        return a < b;
    } catch (...) {
        return a < b; // 解析非纯数字时，退回标准字符串比对
    }
}

int main(int argc, char* argv[]) {
    std::string problemId = "1";
    std::string submissionId = "default";
    std::string srcPath = "../test_cases/1/solution.cpp";
    int timeLimitMs = 1000;          
    int memoryLimitKb = 128 * 1024;   
    int maxOutputSizeKb = 20 * 1024; // 默认 20MB

    int opt;
    while ((opt = getopt(argc, argv, "p:i:s:t:m:o:")) != -1) {
        try {
            switch (opt) {
                case 'p': problemId = optarg; break;
                case 'i': submissionId = optarg; break;
                case 's': srcPath = optarg; break;
                case 't': timeLimitMs = std::stoi(optarg); break;
                case 'm': memoryLimitKb = std::stoi(optarg); break;
                case 'o': maxOutputSizeKb = std::stoi(optarg); break;
                default: break;
            }
        } catch (const std::exception& e) {
            outputJson(Status::SYSTEM_ERROR, 0, 0, "Invalid command line argument value", 0, 0);
            return 0;
        }
    }

    std::string testCasesDir = "../test_cases/" + problemId;
    // 关键修正 1：按 submissionId 创建独立的 workspace 隔离子目录，杜绝并发覆盖！
    std::string workspaceDir = "../workspace/" + submissionId;
    std::string execPath = workspaceDir + "/solution";
    std::string errorMsg;

    // 关键修正 2：带 try-catch 保护的目录创建与扫描
    try {
        fs::create_directories(workspaceDir);
    } catch (const std::exception& e) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, std::string("Failed to create workspace: ") + e.what(), 0, 0);
        return 0;
    }

    try {
        if (!fs::exists(testCasesDir)) {
            outputJson(Status::SYSTEM_ERROR, 0, 0, "Testcase directory not found: " + testCasesDir, 0, 0);
            return 0;
        }
    } catch (const std::exception& e) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, std::string("Directory check error: ") + e.what(), 0, 0);
        return 0;
    }

    // 1. 编译
    if (!Compiler::compileCpp(srcPath, execPath, errorMsg)) {
        outputJson(Status::COMPILE_ERROR, 0, 0, errorMsg, 0, 0);
        return 0;
    }

    // 2. 收集 .in 文件并执行自然数排序
    std::vector<std::string> inFiles;
    try {
        for (const auto& entry : fs::directory_iterator(testCasesDir)) {
            if (entry.path().extension() == ".in") {
                inFiles.push_back(entry.path().stem().string());
            }
        }
    } catch (const std::exception& e) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, std::string("Error reading testcase directory: ") + e.what(), 0, 0);
        return 0;
    }

    std::sort(inFiles.begin(), inFiles.end(), naturalSortCompare);

    int totalCount = inFiles.size();
    if (totalCount == 0) {
        outputJson(Status::SYSTEM_ERROR, 0, 0, "No .in testcases found in " + testCasesDir, 0, 0);
        return 0;
    }

    int maxTimeCost = 0;
    int maxMemoryCost = 0;

    // 3. 循环测试
    for (int i = 0; i < totalCount; ++i) {
        std::string baseName = inFiles[i];
        std::string inputPath = testCasesDir + "/" + baseName + ".in";
        std::string stdOutPath = testCasesDir + "/" + baseName + ".out";
        std::string userOutPath = workspaceDir + "/user_" + baseName + ".out";

        if (!fs::exists(stdOutPath)) {
            outputJson(Status::SYSTEM_ERROR, 0, 0, "Missing corresponding .out file for " + baseName + ".in", i, totalCount);
            return 0;
        }

        JudgeResult runResult = Runner::run(execPath, inputPath, userOutPath, timeLimitMs, memoryLimitKb, maxOutputSizeKb);

        if (runResult.status != Status::ACCEPTED) {
            outputJson(runResult.status, runResult.time_cost_ms, runResult.memory_cost_kb, runResult.error_msg, i, totalCount);
            return 0;
        }

        // 关键修正 3：防止 SIGXFSZ 被用户捕获后绕过，比对前显式校验文件体积
        try {
            uintmax_t outSize = fs::file_size(userOutPath);
            if (outSize > (uintmax_t)maxOutputSizeKb * 1024) {
                outputJson(Status::OUTPUT_LIMIT_EXCEEDED, runResult.time_cost_ms, runResult.memory_cost_kb, "Output file size exceeded limit", i, totalCount);
                return 0;
            }
        } catch (...) {
            outputJson(Status::SYSTEM_ERROR, 0, 0, "Failed to read user output file size", i, totalCount);
            return 0;
        }

        // 比对结果
        if (!Comparer::compare(userOutPath, stdOutPath)) {
            outputJson(Status::WRONG_ANSWER, runResult.time_cost_ms, runResult.memory_cost_kb, "Output mismatch on " + baseName + ".in", i, totalCount);
            return 0;
        }

        maxTimeCost = std::max(maxTimeCost, runResult.time_cost_ms);
        maxMemoryCost = std::max(maxMemoryCost, runResult.memory_cost_kb);
    }

    // 全部通过
    outputJson(Status::ACCEPTED, maxTimeCost, maxMemoryCost, "", totalCount, totalCount);

    return 0;
}