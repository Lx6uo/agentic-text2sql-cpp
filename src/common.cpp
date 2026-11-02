// 普通结构体、少量共用函数。各 CPP 由 main.cpp 统一包含。
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <ctime>
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
volatile sig_atomic_t cancelled = 0;

struct Status {
    bool success = false;
    string error;
};
struct TextResult {
    bool success = false;
    string error;
    string text;
};
struct DatabaseOptions {
    int timeout_ms = 5000;
    int max_display_rows = 100;
    int max_sql_bytes = 16384;
    int max_result_bytes = 1048576;
    int max_preview_rows = 10; // 兼容旧配置，固定流程不上传查询行预览。
    bool reference_double_quoted_strings = false;
};
struct Database {
    sqlite3 *connection = nullptr;
    DatabaseOptions options;
    bool internal = false;
    bool timed_out = false;
    double deadline = 0;
};
struct Cell {
    int type = SQLITE_NULL;
    long long integer = 0;
    double real = 0;
    string text;
};
struct QueryRow {
    Cell *cells = nullptr;
};
// 行数组按本次行数上限一次分配，不再扩容；使用后 free_query。
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
struct AgentOptions {
    int max_sql_attempts = 3;
    int max_steps = 3;
    int max_repairs = 2; // 兼容旧配置中的更低上限。
};
struct Config {
    ApiOptions api;
    AgentOptions agent;
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
    cJSON *messages = nullptr;
};
struct QueryRun {
    bool success = false;
    bool api_error = false;
    string error;
    int attempts = 0;
    QueryResult query;
};
struct BenchmarkOptions {
    string dataset;
    string output;
    string split = "dev";
    int limit = 20;
};
struct BenchmarkStats {
    int total = 0;
    int executed = 0;
    int matched = 0;
};
struct BenchmarkResult {
    bool success = false;
    string error;
    string output;
    BenchmarkStats baseline;
    BenchmarkStats corrected;
};
struct DatasetItem {
    string db_id;
    string question;
    string query;
};
struct DatasetResult {
    bool success = false;
    string error;
    string root;
    string split;
    int count = 0;
    DatasetItem *items = nullptr;
};
struct OutputFile {
    ofstream stream;
};
struct Args {
    bool success = false;
    bool help = false;
    bool benchmark = false;
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
Status failed_status(string error) {
    Status result;
    result.error = error;
    return result;
}
TextResult failed_text(string error) {
    TextResult result;
    result.error = error;
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
