// SQLite 的调用集中在这个文件。结果用 QueryResult 和动态数组保存。
void free_query(QueryResult *result) {
    // 先释放每行的单元格，再释放保存这些行的数组。
    for (int i = 0; i < result->row_count; i++) {
        QueryRow *row = &result->rows[i];
        delete[] row->cells;
    }
    delete[] result->rows;
    result->rows = nullptr;
    result->row_count = 0;
}
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
void close_database(Database *db) {
    if (db->connection != nullptr) {
        sqlite3_progress_handler(db->connection, 0, nullptr, nullptr);
        sqlite3_set_authorizer(db->connection, nullptr, nullptr);
        sqlite3_close(db->connection);
        db->connection = nullptr;
    }
}
Status configure_database(Database *db) {
    DatabaseOptions *options = &db->options;
    int dqs = 0;
    if (options->reference_double_quoted_strings) {
        dqs = 1;
    }
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DQS_DML, dqs, nullptr);
    if (sqlite3_exec(db->connection, "PRAGMA query_only=ON", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
        return failed_status("数据库");
    }
    sqlite3_busy_timeout(db->connection, options->timeout_ms);
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
void install_database(Database *db, Database *opened) {
    close_database(db);
    db->connection = opened->connection;
    opened->connection = nullptr;
    db->options = opened->options;
    db->internal = false;
    sqlite3_set_authorizer(db->connection, database_authorize, db);
}
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
// 各阶段只记录是否成功，函数出口统一报错和清理。
QueryResult execute_sql(Database *db, string sql, int row_limit) {
    QueryResult result;
    result.sql = sql;
    DatabaseOptions *options = &db->options;
    db->deadline = now_ms() + options->timeout_ms;
    db->timed_out = false;
    sqlite3_stmt *statement = nullptr;
    sqlite3_stmt *extra = nullptr;
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
        if (result.row_count == row_limit) {
            result.success = true;
            result.truncated = true;
            break;
        }
        Cell *cells = new (nothrow) Cell[result.column_count];
        if (cells == nullptr) {
            break;
        }
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
QueryResult internal_query(Database *db, string sql) {
    DatabaseOptions *options = &db->options;
    db->internal = true;
    int original_bytes = options->max_result_bytes;
    options->max_result_bytes = 1048576;
    QueryResult result = execute_sql(db, sql, 10000);
    options->max_result_bytes = original_bytes;
    db->internal = false;
    if (result.truncated) {
        result.success = false;
        result.error = error_message("数据库");
        free_query(&result);
    }
    return result;
}
QueryResult list_tables(Database *db) {
    return internal_query(db, "SELECT name, type FROM sqlite_schema WHERE type IN ('table','view') "
                              "AND name NOT GLOB 'sqlite_*' ORDER BY name");
}

TextResult read_schema(Database *db) {
    // 读取表、字段和外键，拼成生成 SQL 时使用的数据库上下文。
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
