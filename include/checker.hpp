#pragma once
#include <string>

enum class CheckStatus {
    MATCH,          // 答案一致
    MISMATCH,       // 答案不一致
    FORMAT_ERROR    // 文件读取失败
};

class Checker {
public:
    // 基础比对：支持忽略行末空格与尾部连续换行
    static CheckStatus strict_diff(const std::string& user_file, const std::string& ans_file);
};