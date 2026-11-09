// SQLite 的调用集中在这个文件。结果用 QueryResult 和动态数组保存。
// 先释放每行的单元格，再释放行数组，可用于失败后的清理。
void free_query(QueryResult *result) {
    for (int i = 0; i < result->row_count; i++) {
        QueryRow *row = &result->rows[i];
        delete[] row->cells;
    }
    delete[] result->rows;
    result->rows = nullptr;
    result->row_count = 0;
}
// SQL 编译时检查操作和函数，拒绝未授权行为。
int database_authorize(void *context, int action, const char *, const char *arg2, const char *,
                       const char *) {
    Database *db = (Database *)context;
    // 内部读取结构时允许 PRAGMA；模型 SQL 只允许查询和列出的函数。
    if (db->internal) {
        return SQLITE_OK;
    }
    if (action == SQLITE_SELECT) {
        return SQLITE_OK;
    }
    if (action == SQLITE_READ) {
        return SQLITE_OK;
    }
    if (action == SQLITE_RECURSIVE) {
        return SQLITE_OK;
    }
    if (action == SQLITE_FUNCTION && arg2 != nullptr) {
        const char *allowed[] = {"abs",
                                 "avg",
                                 "count",
                                 "sum",
                                 "total",
                                 "min",
                                 "max",
                                 "round",
                                 "coalesce",
                                 "ifnull",
                                 "nullif",
                                 "lower",
                                 "upper",
                                 "length",
                                 "octet_length",
                                 "substr",
                                 "substring",
                                 "trim",
                                 "ltrim",
                                 "rtrim",
                                 "replace",
                                 "instr",
                                 "like",
                                 "glob",
                                 "concat",
                                 "concat_ws",
                                 "format",
                                 "printf",
                                 "unicode",
                                 "char",
                                 "hex",
                                 "unhex",
                                 "typeof",
                                 "quote",
                                 "group_concat",
                                 "string_agg",
                                 "date",
                                 "time",
                                 "datetime",
                                 "julianday",
                                 "unixepoch",
                                 "strftime",
                                 "timediff",
                                 "row_number",
                                 "rank",
                                 "dense_rank",
                                 "percent_rank",
                                 "cume_dist",
                                 "ntile",
                                 "lag",
                                 "lead",
                                 "first_value",
                                 "last_value",
                                 "nth_value",
                                 "json",
                                 "json_valid",
                                 "json_extract",
                                 "json_array_length",
                                 "json_type",
                                 "json_quote",
                                 "json_array",
                                 "json_object",
                                 "json_group_array",
                                 "json_group_object"};
        string name = arg2;
        for (int i = 0; i < (int)name.length(); i++) {
            name[i] = (char)tolower((unsigned char)name[i]);
        }
        int count = sizeof(allowed) / sizeof(allowed[0]);
        for (int i = 0; i < count; i++) {
            if (name == allowed[i]) {
                return SQLITE_OK;
            }
        }
    }
    return SQLITE_DENY;
}
// 执行中检查取消和超时，返回非零值通知 SQLite 中止。
int database_progress(void *context) {
    Database *db = (Database *)context;
    if (cancelled) {
        return 1;
    }
    if (now_ms() >= db->deadline) {
        db->timed_out = true;
        return 1;
    }
    return 0;
}
// 先移除回调，再关闭连接并清空句柄。
void close_database(Database *db) {
    if (db->connection != nullptr) {
        sqlite3_progress_handler(db->connection, 0, nullptr, nullptr);
        sqlite3_set_authorizer(db->connection, nullptr, nullptr);
        sqlite3_close(db->connection);
        db->connection = nullptr;
    }
}
// 只读设置、授权回调和资源上限共同约束模型 SQL。
Status configure_database(Database *db) {
    DatabaseOptions *options = &db->options;
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DQS_DML, 0, nullptr);
    if (sqlite3_exec(db->connection, "PRAGMA query_only=ON", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
        return failed_status("数据库");
    }
    sqlite3_busy_timeout(db->connection, options->timeout_ms);
    // 限制 SQL、结果大小和执行复杂度，禁止附加数据库。
    sqlite3_limit(db->connection, SQLITE_LIMIT_LENGTH, 1048576);
    sqlite3_limit(db->connection, SQLITE_LIMIT_SQL_LENGTH, options->max_sql_bytes);
    sqlite3_limit(db->connection, SQLITE_LIMIT_COLUMN, 128);
    sqlite3_limit(db->connection, SQLITE_LIMIT_EXPR_DEPTH, 100);
    sqlite3_limit(db->connection, SQLITE_LIMIT_COMPOUND_SELECT, 20);
    sqlite3_limit(db->connection, SQLITE_LIMIT_VDBE_OP, 250000);
    sqlite3_limit(db->connection, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_set_authorizer(db->connection, database_authorize, db);
    return ok_status();
}
// 用已验证的新连接替换原连接。
void install_database(Database *db, Database *opened) {
    close_database(db);
    // 转移连接所有权，来源对象不再负责关闭它。
    db->connection = opened->connection;
    opened->connection = nullptr;
    db->options = opened->options;
    db->internal = false;
    // 回调绑定到接收连接的对象，避免引用局部变量。
    sqlite3_set_authorizer(db->connection, database_authorize, db);
}
// 以只读方式打开已有数据库，验证结构后才安装连接。
Status open_database(Database *db, string path, DatabaseOptions options) {
    bool valid = true;
    if (path.empty()) {
        valid = false;
    }
    if (path == ":memory:") {
        valid = false;
    }
    if (path.find('\0') != string::npos) {
        valid = false;
    }
    if (valid == false) {
        return failed_status("数据库");
    }
    // 新连接验证通过后才替换原连接。
    Database opened;
    opened.options = options;
    int code = sqlite3_open_v2(path.c_str(), &opened.connection,
                               SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    Status state;
    if (code == SQLITE_OK) {
        state = configure_database(&opened);
        if (state.success) {
            QueryResult tables = list_tables(&opened);
            state.success = tables.success;
            free_query(&tables);
        }
    }
    if (state.success == false) {
        close_database(&opened);
        return failed_status("数据库");
    }
    install_database(db, &opened);
    return ok_status();
}
// 复制 SQLite 临时数据，避免下一次 step 后失效。
Cell read_cell(sqlite3_stmt *statement, int column) {
    Cell result;
    result.type = sqlite3_column_type(statement, column);
    if (result.type == SQLITE_INTEGER) {
        result.integer = sqlite3_column_int64(statement, column);
    }
    if (result.type == SQLITE_FLOAT) {
        result.real = sqlite3_column_double(statement, column);
    }
    if (result.type == SQLITE_TEXT) {
        const char *text = (const char *)sqlite3_column_text(statement, column);
        int bytes = sqlite3_column_bytes(statement, column);
        if (text != nullptr) {
            result.text.assign(text, bytes);
        }
    }
    // BLOB 转为十六进制保存，便于显示和逐值比较。
    if (result.type == SQLITE_BLOB) {
        const unsigned char *bytes = (const unsigned char *)sqlite3_column_blob(statement, column);
        const char *digits = "0123456789abcdef";
        for (int i = 0; i < sqlite3_column_bytes(statement, column); i++) {
            result.text += digits[bytes[i] / 16];
            result.text += digits[bytes[i] % 16];
        }
    }
    return result;
}
// 执行单条只读 SQL，按行数、容量和超时限制读取结果。
QueryResult execute_sql(Database *db, string sql, int row_limit) {
    QueryResult result;
    result.sql = sql;
    DatabaseOptions *options = &db->options;
    db->deadline = now_ms() + options->timeout_ms;
    db->timed_out = false;
    sqlite3_stmt *statement = nullptr;
    sqlite3_stmt *extra = nullptr;
    // 先校验输入和读取上限，再交给 SQLite 编译检查。
    bool ready = db->connection != nullptr && !cancelled;
    if ((int)sql.length() > options->max_sql_bytes) {
        ready = false;
    }
    if (sql.find('\0') != string::npos) {
        ready = false;
    }
    if (row_limit < 0) {
        row_limit = options->max_display_rows;
    }
    if (row_limit < 1 || row_limit > 100000) {
        ready = false;
    }
    if (ready) {
        // 开启进度回调，编译时检查单条语句、只读性和列数。
        sqlite3_progress_handler(db->connection, 1000, database_progress, db);
        const char *tail = nullptr;
        int code = sqlite3_prepare_v3(db->connection, sql.c_str(), (int)sql.length() + 1, 0,
                                      &statement, &tail);
        ready = code == SQLITE_OK && statement != nullptr;
        if (ready && tail != nullptr) {
            // 检查剩余文本，防止一次模型回复包含第二条 SQL。
            code = sqlite3_prepare_v3(db->connection, tail, -1, 0, &extra, nullptr);
            ready = code == SQLITE_OK && extra == nullptr;
        }
        if (ready) {
            ready = sqlite3_stmt_readonly(statement) != 0;
            result.column_count = sqlite3_column_count(statement);
            if (result.column_count < 1 || result.column_count > 128) {
                ready = false;
            }
        }
    }
    if (ready) {
        for (int c = 0; c < result.column_count; c++) {
            result.columns[c] = sqlite3_column_name(statement, c);
        }
        // 行数组一次分配，单元格在读取时逐行分配。
        result.rows = new (nothrow) QueryRow[row_limit];
        ready = result.rows != nullptr;
    }
    int bytes = 0;
    // SQLite 每次返回一行，到达行数或大小上限时停止并标记截断。
    while (ready) {
        if (now_ms() >= db->deadline) {
            db->timed_out = true;
            break;
        }
        if (cancelled) {
            break;
        }
        int code = sqlite3_step(statement);
        if (code == SQLITE_DONE) {
            result.success = true;
            break;
        }
        if (code != SQLITE_ROW) {
            break;
        }
        // 多读一行，确认是否仍有数据后再标记截断。
        if (result.row_count == row_limit) {
            result.success = true;
            result.truncated = true;
            break;
        }
        Cell *cells = new (nothrow) Cell[result.column_count];
        if (cells == nullptr) {
            break;
        }
        // 估算本行容量，超限时保留此前读取的完整行。
        int row_bytes = 2;
        for (int c = 0; c < result.column_count; c++) {
            cells[c] = read_cell(statement, c);
            row_bytes += cell_bytes(&cells[c]);
            if (c > 0) {
                row_bytes++;
            }
        }
        if (bytes + row_bytes > options->max_result_bytes) {
            delete[] cells;
            result.success = true;
            result.truncated = true;
            break;
        }
        QueryRow *row = &result.rows[result.row_count];
        row->cells = cells;
        result.row_count++;
        bytes += row_bytes;
    }
    if (now_ms() >= db->deadline) {
        db->timed_out = true;
    }
    result.timed_out = db->timed_out;
    result.cancelled = cancelled != 0;
    if (result.timed_out || result.cancelled) {
        result.success = false;
    }
    // 所有执行分支统一释放语句、移除回调，并清理失败结果。
    sqlite3_finalize(extra);
    sqlite3_finalize(statement);
    if (db->connection != nullptr) {
        sqlite3_progress_handler(db->connection, 0, nullptr, nullptr);
    }
    if (result.success == false) {
        free_query(&result);
        result.error = error_message("SQL");
    }
    return result;
}
// 标识符用双引号包围，内部双引号重复转义。
string quote_name(string name) {
    string result = "\"";
    for (int i = 0; i < (int)name.length(); i++) {
        result += name[i];
        if (name[i] == '"') {
            result += '"';
        }
    }
    return result + '"';
}
// 结构查询使用内部授权和固定容量，结束后恢复原设置。
QueryResult internal_query(Database *db, string sql) {
    DatabaseOptions *options = &db->options;
    db->internal = true;
    int original_bytes = options->max_result_bytes;
    options->max_result_bytes = 1048576;
    QueryResult result = execute_sql(db, sql, 10000);
    options->max_result_bytes = original_bytes;
    db->internal = false;
    // 结构信息必须完整，截断时按读取失败处理。
    if (result.truncated) {
        result.success = false;
        result.error = error_message("数据库");
        free_query(&result);
    }
    return result;
}
// 读取用户表和视图，排除 SQLite 自带对象。
QueryResult list_tables(Database *db) {
    return internal_query(db, "SELECT name, type FROM sqlite_schema WHERE type IN ('table','view') "
                              "AND name NOT GLOB 'sqlite_*' ORDER BY name");
}

// 读取表、字段和外键，拼成生成 SQL 的数据库上下文。
TextResult read_schema(Database *db) {
    TextResult result;
    QueryResult tables = list_tables(db);
    if (!tables.success) {
        return failed_text("数据库");
    }
    for (int i = 0; i < tables.row_count; i++) {
        QueryRow *row = &tables.rows[i];
        Cell *table = row->cells;
        string name = table[0].text;
        result.text += "表 " + quote_name(name) + " (" + table[1].text + ")\n";
        QueryResult columns = internal_query(db, "PRAGMA table_xinfo(" + quote_name(name) + ")");
        if (!columns.success) {
            result.error = columns.error;
        }
        for (int r = 0; r < columns.row_count; r++) {
            QueryRow *row = &columns.rows[r];
            Cell *column = row->cells;
            result.text += "  " + quote_name(column[1].text) + " " + column[2].text;
            if (column[3].integer) {
                result.text += " NOT NULL";
            }
            if (column[5].integer) {
                result.text += " PRIMARY KEY " + to_string(column[5].integer);
            }
            result.text += "\n";
        }
        free_query(&columns);
        QueryResult keys = internal_query(db, "PRAGMA foreign_key_list(" + quote_name(name) + ")");
        if (!keys.success) {
            result.error = keys.error;
        }
        for (int r = 0; r < keys.row_count; r++) {
            QueryRow *row = &keys.rows[r];
            Cell *key = row->cells;
            result.text += "  外键 " + quote_name(key[3].text) + " -> " + quote_name(key[2].text) +
                           "." + quote_name(key[4].text) + "\n";
        }
        free_query(&keys);
        // 结构上下文也有容量上限，避免模型请求过大。
        if (result.text.length() > 500000) {
            result.error = error_message("数据库");
        }
        if (!result.error.empty()) {
            break;
        }
    }
    free_query(&tables);
    result.success = result.error.empty();
    if (result.success == false) {
        result.error = error_message("数据库");
    }
    return result;
}
