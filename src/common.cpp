// 普通结构体、少量共用函数。各 CPP 由 main.cpp 统一包含。
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <cctype>
#include <cstdlib>
#include <new>
#include <curl/curl.h>
#include <cJSON.h>
#include <sqlite3.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
using namespace std;
// 信号处理只设标记，由 HTTP 和 SQLite 回调响应取消。
volatile sig_atomic_t cancelled = 0;

// 通用返回状态：success 表示成功，error 保存失败提示。
struct Status {
    bool success = false;
    string error;
};
struct TextResult {
    bool success = false;
    string error;
    string text;
};
// 查询和结果分析使用的超时、容量与行数限制。
struct DatabaseOptions {
    int timeout_ms = 5000;
    int max_display_rows = 100;
    int max_sql_bytes = 16384;
    int max_result_bytes = 1048576;
    int max_preview_rows = 100;
};
struct Database {
    sqlite3 *connection = nullptr;
    DatabaseOptions options;
    bool internal = false;
    bool timed_out = false;
    double deadline = 0;
};
// 保留 SQLite 数据类型，供结果显示和分析使用。
struct Cell {
    int type = SQLITE_NULL;
    long long integer = 0;
    double real = 0;
    string text;
};
struct QueryRow {
    Cell *cells = nullptr;
};
// 行数组一次分配，由调用者使用后调用 free_query 释放。
struct QueryResult {
    bool success = false;
    string error;
    string sql;
    int column_count = 0;
    string columns[128];
    int row_count = 0;
    QueryRow *rows = nullptr;
    bool truncated = false;
    bool timed_out = false;
    bool cancelled = false;
};
struct ApiOptions {
    string base_url = "https://api.deepseek.com";
    string model = "deepseek-flash";
    string api_key;
    int timeout_seconds = 45;
    int max_tokens = 2048;
};
struct Config {
    ApiOptions api;
    DatabaseOptions database;
    string database_path;
    string bundled_data;
};
struct ConfigResult {
    bool success = false;
    string error;
    Config config;
};
struct Completion {
    ApiOptions api;
    // messages 保存本次请求上下文，用完后由 close_completion 释放。
    cJSON *messages = nullptr;
};
struct QueryRun {
    bool success = false;
    bool api_error = false;
    string error;
    bool invalid_input = false;
    // 查询和分析分别记录状态，分析失败时仍可显示查询结果。
    TextResult analysis;
    QueryResult query;
};
// 区分默认参数和显式参数，便于校验及确定优先级。
struct Args {
    bool success = false;
    bool help = false;
    bool config_required = false;
    bool db_given = false;
    string error;
    string config = "config.json";
    string db;
};
Status ok_status() {
    Status result;
    result.success = true;
    return result;
}
// 所有模块在出口使用这些统一错误信息，由 CLI 显示。
string error_message(string operation) {
    if (cancelled) {
        return "操作已取消。";
    }
    if (operation == "API") {
        return "模型请求失败，请检查 API 配置或网络。";
    }
    if (operation == "配置") {
        return "配置读取失败，请检查文件路径和 JSON 字段。";
    }
    if (operation == "参数") {
        return "参数有误，请检查启动选项或输入值。";
    }
    if (operation == "文件") {
        return "文件操作失败，请检查路径和文件是否可读写。";
    }
    if (operation == "数据库") {
        return "数据库打开或读取失败，请检查 SQLite 文件。";
    }
    if (operation == "SQL") {
        return "SQL 执行失败，请检查查询语句。";
    }
    return "数据处理失败，请检查输入。";
}
Status failed_status(string operation) {
    Status result;
    result.error = error_message(operation);
    return result;
}
TextResult failed_text(string operation) {
    TextResult result;
    result.error = error_message(operation);
    return result;
}
bool is_blank(char value) {
    if (value == ' ') {
        return true;
    }
    if (value == '\t') {
        return true;
    }
    if (value == '\r') {
        return true;
    }
    if (value == '\n') {
        return true;
    }
    return false;
}
string trim(string text) {
    int first = 0;
    int last = (int)text.length() - 1;
    while (first <= last) {
        if (is_blank(text[first]) == false) {
            break;
        }
        first++;
    }
    while (last >= first) {
        if (is_blank(text[last]) == false) {
            break;
        }
        last--;
    }
    if (last < first) {
        return "";
    }
    return text.substr(first, last - first + 1);
}
double now_ms() {
    // 单调时钟不受系统时间调整影响，适合判断超时。
#ifdef _WIN32
    return (double)GetTickCount64();
#else
    timespec value;
    clock_gettime(CLOCK_MONOTONIC, &value);
    return value.tv_sec * 1000.0 + value.tv_nsec / 1000000.0;
#endif
}
QueryResult execute_sql(Database *db, string sql, int row_limit = -1);
QueryResult list_tables(Database *db);

struct NumberResult {
    bool success = false;
    string error;
    double number = 0;
};
