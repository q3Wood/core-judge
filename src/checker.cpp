#include "checker.hpp"
#include <fstream>
#include <string>
#include <algorithm>

// 辅助函数：去除一行字符串末尾的所有空白字符（包括空格、制表符 \t、Windows换行符 \r）
static void trim_trailing_spaces(std::string& s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.pop_back();
    }
}

CheckStatus Checker::strict_diff(const std::string& user_file, const std::string& ans_file) {
    std::ifstream f_user(user_file);
    std::ifstream f_ans(ans_file);

    // 1. 文件打开异常检查
    if (!f_user.is_open() || !f_ans.is_open()) {
        return CheckStatus::FORMAT_ERROR;
    }

    std::string line_user, line_ans;

    // 2. 逐行比对核心循环
    while (true) {
        bool has_user = static_cast<bool>(std::getline(f_user, line_user));
        bool has_ans  = static_cast<bool>(std::getline(f_ans, line_ans));

        // 两个文件都已经读取完毕
        if (!has_user && !has_ans) {
            return CheckStatus::MATCH;
        }

        // 一个文件读完了，另一个文件还没读完
        // 规则：只要剩下的行全都是空行或纯空格，依然算正确；否则算 WA
        if (!has_user && has_ans) {
            trim_trailing_spaces(line_ans);
            if (!line_ans.empty()) return CheckStatus::MISMATCH;
            while (std::getline(f_ans, line_ans)) {
                trim_trailing_spaces(line_ans);
                if (!line_ans.empty()) return CheckStatus::MISMATCH;
            }
            return CheckStatus::MATCH;
        }

        if (has_user && !has_ans) {
            trim_trailing_spaces(line_user);
            if (!line_user.empty()) return CheckStatus::MISMATCH;
            while (std::getline(f_user, line_user)) {
                trim_trailing_spaces(line_user);
                if (!line_user.empty()) return CheckStatus::MISMATCH;
            }
            return CheckStatus::MATCH;
        }

        // 两个文件都有内容，去掉末尾空格后进行严格比对
        trim_trailing_spaces(line_user);
        trim_trailing_spaces(line_ans);

        if (line_user != line_ans) {
            return CheckStatus::MISMATCH;
        }
    }
}