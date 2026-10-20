#include "database.h"
#include <algorithm>
#include <cctype>
#include <set>
#include <stdexcept>

namespace t2s {
namespace {
struct Statement {
    sqlite3_stmt* ptr = nullptr;
    ~Statement() { sqlite3_finalize(ptr); }
};
std::string quote(const std::string& name) {
    std::string out = "\"";
    for (char c : name) { out += c; if (c == '"') out += c; }
    return out + '"';
}
Json cell(sqlite3_stmt* stmt, int c) {
    switch (sqlite3_column_type(stmt, c)) {
    case SQLITE_NULL: return nullptr;
    case SQLITE_INTEGER: return sqlite3_column_int64(stmt, c);
    case SQLITE_FLOAT: return sqlite3_column_double(stmt, c);
    case SQLITE_BLOB: {
        static const char* digits = "0123456789abcdef";
        auto* bytes = static_cast<const unsigned char*>(sqlite3_column_blob(stmt,c));
        std::string hex;
        for (int i = 0; i < sqlite3_column_bytes(stmt,c); ++i) {
            hex += digits[bytes[i] >> 4]; hex += digits[bytes[i] & 15];
        }
        return Json{{"blob_hex",hex}};
    }
    default:
        return std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt,c)), sqlite3_column_bytes(stmt,c));
    }
}
}
Database::Database(DatabaseOptions options): options_(options) {
    if (sqlite3_open_v2(":memory:", &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
        std::string error = db_ ? sqlite3_errmsg(db_) : "allocation failed";
        sqlite3_close(db_); db_ = nullptr;
        throw std::runtime_error("Cannot open demo database: " + error);
    }
}
Database::Database(const std::string& path, DatabaseOptions options): options_(options) {
    if (path.empty() || path == ":memory:" || path.find('\0') != std::string::npos)
        throw std::runtime_error("Expected an existing SQLite file path");
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr) != SQLITE_OK) {
        std::string error = db_ ? sqlite3_errmsg(db_) : "allocation failed";
        sqlite3_close(db_); db_ = nullptr;
        throw std::runtime_error("Cannot open database: " + error);
    }
    try { harden(); list_tables(); }
    catch (...) { sqlite3_close(db_); db_ = nullptr; throw; }
}
Database::~Database() { sqlite3_close(db_); }
std::unique_ptr<Database> Database::from_sql(const std::string& sql, DatabaseOptions options) {
    auto db = std::unique_ptr<Database>(new Database(options));
    char* error = nullptr;
    if (sqlite3_exec(db->db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : "Demo initialization failed";
        sqlite3_free(error); throw std::runtime_error(message);
    }
    db->harden(); return db;
}
void Database::harden() {
    if (options_.timeout_ms < 1 || options_.max_display_rows < 1 || options_.max_preview_rows < 1 ||
        options_.max_sql_bytes < 1 || options_.max_result_bytes < 1)
        throw std::runtime_error("Invalid database limits");
    sqlite3_db_config(db_, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db_, SQLITE_DBCONFIG_DQS_DML, options_.reference_double_quoted_strings ? 1 : 0, nullptr);
    sqlite3_exec(db_, "PRAGMA query_only=ON", nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(db_, options_.timeout_ms);
    sqlite3_limit(db_, SQLITE_LIMIT_LENGTH, 1048576);
    sqlite3_limit(db_, SQLITE_LIMIT_SQL_LENGTH, options_.max_sql_bytes);
    sqlite3_limit(db_, SQLITE_LIMIT_COLUMN, 128);
    sqlite3_limit(db_, SQLITE_LIMIT_EXPR_DEPTH, 100);
    sqlite3_limit(db_, SQLITE_LIMIT_COMPOUND_SELECT, 20);
    sqlite3_limit(db_, SQLITE_LIMIT_VDBE_OP, 250000);
    sqlite3_limit(db_, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_set_authorizer(db_, authorize, this);
}
int Database::authorize(void* context, int action, const char*, const char* arg2, const char*, const char*) {
    auto& db = *static_cast<Database*>(context);
    if (db.internal_) return SQLITE_OK;
    if (action == SQLITE_SELECT || action == SQLITE_READ || action == SQLITE_RECURSIVE) return SQLITE_OK;
    if (action == SQLITE_FUNCTION && arg2) {
        static const std::set<std::string> allowed = {
            "abs","avg","count","sum","total","min","max","round","coalesce","ifnull","nullif",
            "lower","upper","length","octet_length","substr","substring","trim","ltrim","rtrim","replace",
            "instr","like","glob","concat","concat_ws","format","printf","unicode","char","hex","unhex",
            "typeof","quote","group_concat","string_agg","date","time","datetime","julianday","unixepoch",
            "strftime","timediff","row_number","rank","dense_rank","percent_rank","cume_dist","ntile",
            "lag","lead","first_value","last_value","nth_value","json","json_valid","json_extract",
            "json_array_length","json_type","json_quote","json_array","json_object","json_group_array","json_group_object"
        };
        std::string name = arg2;
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::tolower(c);});
        if (allowed.contains(name)) return SQLITE_OK;
    }
    return SQLITE_DENY;
}
int Database::progress(void* context) {
    auto& db = *static_cast<Database*>(context);
    if (cancelled) return 1;
    if (Clock::now() >= db.deadline_) { db.timed_out_ = true; return 1; }
    return 0;
}
Json Database::internal_query(const std::string& sql) {
    struct Guard { bool& flag; Guard(bool& f):flag(f){flag=true;} ~Guard(){flag=false;} } guard(internal_);
    timed_out_=false;deadline_=Clock::now()+std::chrono::milliseconds(options_.timeout_ms);
    sqlite3_progress_handler(db_,1000,progress,this);
    struct ProgressGuard {sqlite3* db;~ProgressGuard(){sqlite3_progress_handler(db,0,nullptr,nullptr);}} progress_guard{db_};
    Statement stmt;
    if (sqlite3_prepare_v3(db_, sql.c_str(), -1, 0, &stmt.ptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db_));
    Json out = Json::array(); int status;size_t bytes=0;
    while ((status = sqlite3_step(stmt.ptr)) == SQLITE_ROW) {
        if(cancelled)throw std::runtime_error("Schema read cancelled");
        if(Clock::now()>=deadline_)throw std::runtime_error("Schema read timed out");
        Json row = Json::object();
        for (int c=0;c<sqlite3_column_count(stmt.ptr);++c) row[sqlite3_column_name(stmt.ptr,c)] = cell(stmt.ptr,c);
        bytes+=row.dump(-1,' ',false,Json::error_handler_t::replace).size();
        if(bytes>1048576)throw std::runtime_error("Schema exceeds 1 MB");
        out.push_back(std::move(row));
        if (out.size() > 10000) throw std::runtime_error("Schema exceeds 10000 entries");
    }
    if (status != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    return out;
}
Json Database::list_tables() {
    return internal_query("SELECT name, type FROM sqlite_schema WHERE type IN ('table','view') AND name NOT GLOB 'sqlite_*' ORDER BY name");
}
Json Database::describe_tables(const std::vector<std::string>& names) {
    auto tables = list_tables(); Json result = Json::array();size_t bytes=0;
    for (const auto& name : names) {
        auto found = std::find_if(tables.begin(), tables.end(), [&](const Json& t){return t["name"]==name;});
        if (found == tables.end()) throw std::runtime_error("Unknown table: " + name);
        result.push_back(Json{{"name",name},{"type",(*found)["type"]},
            {"columns",internal_query("PRAGMA table_xinfo(" + quote(name) + ")")},
            {"foreign_keys",internal_query("PRAGMA foreign_key_list(" + quote(name) + ")")}});
        bytes+=result.back().dump(-1,' ',false,Json::error_handler_t::replace).size();
        if(bytes>1048576)throw std::runtime_error("Schema exceeds 1 MB; select fewer tables");
    }
    return result;
}
Json Database::schema() {
    std::vector<std::string> names;
    for (const auto& t : list_tables()) names.push_back(t.at("name").get<std::string>());
    return describe_tables(names);
}
Json Database::execute(const std::string& sql, int row_limit) {
    auto start = Clock::now(); timed_out_ = false;
    deadline_ = start + std::chrono::milliseconds(options_.timeout_ms);
    Json result{{"success",false},{"sql",sql},{"columns",Json::array()},{"rows",Json::array()},
        {"error",nullptr},{"execution_ms",0.0},{"truncated",false},{"query_id",++query_id_},
        {"timed_out",false},{"cancelled",false}};
    sqlite3_progress_handler(db_, 1000, progress, this);
    try {
        if (cancelled) throw std::runtime_error("Query cancelled");
        if (sql.size() > static_cast<size_t>(options_.max_sql_bytes)) throw std::runtime_error("SQL exceeds length limit");
        if (sql.find('\0') != std::string::npos) throw std::runtime_error("SQL contains NUL");
        Statement stmt; const char* tail=nullptr;
        if (sqlite3_prepare_v3(db_, sql.c_str(), static_cast<int>(sql.size()+1), 0, &stmt.ptr, &tail) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db_));
        if (!stmt.ptr) throw std::runtime_error("Empty SQL");
        Statement extra;
        if (tail && (sqlite3_prepare_v3(db_, tail, -1, 0, &extra.ptr, nullptr) != SQLITE_OK || extra.ptr))
            throw std::runtime_error("Multiple SQL statements are forbidden");
        if (!sqlite3_stmt_readonly(stmt.ptr)) throw std::runtime_error("Only read-only SQL is allowed");
        if (Clock::now() >= deadline_) { timed_out_=true; throw std::runtime_error("Query timed out"); }
        int count = sqlite3_column_count(stmt.ptr);
        if (!count) throw std::runtime_error("Expected a result-producing query");
        for (int c=0;c<count;++c) result["columns"].push_back(sqlite3_column_name(stmt.ptr,c));
        int limit = row_limit < 0 ? options_.max_display_rows : row_limit;
        if (limit < 1) throw std::runtime_error("Invalid row limit");
        size_t bytes = 0; int status;
        while ((status=sqlite3_step(stmt.ptr)) == SQLITE_ROW) {
            if (elapsed_ms(start) >= options_.timeout_ms) { timed_out_=true; throw std::runtime_error("Query timed out"); }
            if (cancelled) throw std::runtime_error("Query cancelled");
            if (result["rows"].size() >= static_cast<size_t>(limit)) { result["truncated"]=true; break; }
            Json row = Json::array();
            for (int c=0;c<count;++c) row.push_back(cell(stmt.ptr,c));
            bytes += row.dump(-1,' ',false,Json::error_handler_t::replace).size();
            if (bytes > static_cast<size_t>(options_.max_result_bytes)) {result["truncated"]=true; break;}
            result["rows"].push_back(std::move(row));
        }
        if (status != SQLITE_DONE && status != SQLITE_ROW) throw std::runtime_error(sqlite3_errmsg(db_));
        if (Clock::now() >= deadline_) { timed_out_=true; throw std::runtime_error("Query timed out"); }
        result["success"] = true;
    } catch (const std::exception& e) {
        result["error"] = timed_out_ ? "Query timed out" : (cancelled ? "Query cancelled" : e.what());
        result["rows"] = Json::array();
    }
    sqlite3_progress_handler(db_,0,nullptr,nullptr);
    result["execution_ms"]=elapsed_ms(start); result["timed_out"]=timed_out_; result["cancelled"]=cancelled!=0;
    return result;
}
}
