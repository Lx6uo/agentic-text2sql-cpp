#pragma once
#include "common.h"
#include <memory>
#include <sqlite3.h>

namespace t2s {
struct DatabaseOptions {
    int timeout_ms = 5000;
    int max_display_rows = 100;
    int max_preview_rows = 10;
    int max_sql_bytes = 16384;
    int max_result_bytes = 1048576;
};
class Database {
public:
    explicit Database(const std::string& path, DatabaseOptions options = {});
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    static std::unique_ptr<Database> from_sql(const std::string& sql, DatabaseOptions options = {});
    Json list_tables();
    Json describe_tables(const std::vector<std::string>& names);
    Json schema();
    Json execute(const std::string& sql, int row_limit = -1);
    const DatabaseOptions& options() const { return options_; }
private:
    Database(DatabaseOptions options);
    void harden();
    Json internal_query(const std::string& sql);
    static int authorize(void*, int, const char*, const char*, const char*, const char*);
    static int progress(void*);
    sqlite3* db_ = nullptr;
    DatabaseOptions options_;
    bool internal_ = false;
    bool timed_out_ = false;
    Clock::time_point deadline_;
    unsigned query_id_ = 0;
};
}
