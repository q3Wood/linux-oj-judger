#include "Comparer.hpp"
#include <fstream>
#include <string>

bool Comparer::compare(const std::string& userOutPath, const std::string& stdOutPath) {
    std::ifstream userFile(userOutPath);
    std::ifstream stdFile(stdOutPath);

    if (!userFile.is_open() || !stdFile.is_open()) {
        return false;
    }

    std::string userWord, stdWord;
    
    // 按单词/ Token 逐个比对（忽略末尾多余的空格和换行，符合标准 OJ 规则）
    while (userFile >> userWord) {
        if (!(stdFile >> stdWord)) return false; // 用户输出了多余内容
        if (userWord != stdWord) return false;     // 内容不匹配
    }

    // 检查标准答案文件是否还有未读完的内容
    if (stdFile >> stdWord) return false;

    return true;
}