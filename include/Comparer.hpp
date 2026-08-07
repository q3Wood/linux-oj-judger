#ifndef COMPARER_HPP
#define COMPARER_HPP

#include <string>

class Comparer {
public:
    // 比对用户输出文件与标准答案文件，一致返回 true，不一致返回 false
    static bool compare(const std::string& userOutPath, const std::string& stdOutPath);
};

#endif