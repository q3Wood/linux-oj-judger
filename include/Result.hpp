#ifndef RESULT_HPP
#define RESULT_HPP

#include <string>

// 判定结果状态码 (Status Code)
enum class Status {
    ACCEPTED,               // AC: 通过
    WRONG_ANSWER,           // WA: 答案错误
    COMPILE_ERROR,          // CE: 编译错误
    TIME_LIMIT_EXCEEDED,    // TLE: 超时
    MEMORY_LIMIT_EXCEEDED,  // MLE: 内存超限
    RUNTIME_ERROR,          // RE: 运行时错误
    SYSTEM_ERROR,           // SE: 系统故障
    OUTPUT_LIMIT_EXCEEDED,  // OLE: 输出超限
};

// 判题详情结构体
struct JudgeResult {
    Status status;          // 最终状态
    int time_cost_ms;       // 耗时 (毫秒)
    int memory_cost_kb;     // 内存 (KB)
    std::string error_msg;  // 错误日志 (如编译报错信息)
};

#endif