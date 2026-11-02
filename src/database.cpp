// SQLite 的调用集中在这个文件。结果用 QueryResult 和动态数组保存。
void free_query(QueryResult *result) {
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
    bool invalid = false;
    if (options->timeout_ms < 1) {
        invalid = true;
    }
    if (options->max_display_rows < 1) {
        invalid = true;
    }
    if (options->max_preview_rows < 1) {
        invalid = true;
    }
    if (options->max_sql_bytes < 1) {
        invalid = true;
    }
    if (options->max_result_bytes < 1) {
        invalid = true;
    }
    if (invalid) {
        return failed_status("Invalid database limits");
    }
    int dqs = 0;
    if (options->reference_double_quoted_strings) {
        dqs = 1;
    }
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DQS_DML, dqs, nullptr);
    if (sqlite3_exec(db->connection, "PRAGMA query_only=ON", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
        return failed_status("Cannot configure read-only database");
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
    if (path.empty()) {
        return failed_status("Expected an existing SQLite file path");
    }
    if (path == ":memory:") {
        return failed_status("Expected an existing SQLite file path");
    }
    if (path.find('\0') != string::npos) {
        return failed_status("Expected an existing SQLite file path");
    }
    Database opened;
    opened.options = options;
    int code = sqlite3_open_v2(path.c_str(), &opened.connection,
                               SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    if (code != SQLITE_OK) {
        string error = "Cannot allocate database";
        if (opened.connection != nullptr) {
            error = sqlite3_errmsg(opened.connection);
        }
        close_database(&opened);
        return failed_status("Cannot open database: " + error);
    }
    Status result = configure_database(&opened);
    if (result.success) {
        QueryResult tables = list_tables(&opened);
        if (!tables.success) {
            result = failed_status(tables.error);
        }
        free_query(&tables);
    }
    if (!result.success) {
        close_database(&opened);
        return result;
    }
    install_database(db, &opened);
    return ok_status();
}
Status create_demo_database(Database *db, string sql, DatabaseOptions options) {
    Database opened;
    opened.options = options;
    int code =
        sqlite3_open_v2(":memory:", &opened.connection,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
    if (code != SQLITE_OK) {
        close_database(&opened);
        return failed_status("Cannot open demo database");
    }
    char *error = nullptr;
    code = sqlite3_exec(opened.connection, sql.c_str(), nullptr, nullptr, &error);
    if (code != SQLITE_OK) {
        string message = "Demo initialization failed";
        if (error != nullptr) {
            message = error;
        }
        sqlite3_free(error);
        close_database(&opened);
        return failed_status(message);
    }
    Status result = configure_database(&opened);
    if (!result.success) {
        close_database(&opened);
        return result;
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
QueryResult execute_sql(Database *db, string sql, int row_limit) {
    QueryResult result;
    result.sql = sql;
    double start = now_ms();
    DatabaseOptions *options = &db->options;
    db->deadline = start + options->timeout_ms;
    db->timed_out = false;
    sqlite3_stmt *statement = nullptr;
    sqlite3_stmt *extra = nullptr;
    if (db->connection == nullptr) {
        result.error = "Database is not open";
    } else if (cancelled) {
        result.error = "Query cancelled";
    } else if ((int)sql.length() > options->max_sql_bytes) {
        result.error = "SQL exceeds length limit";
    } else if (sql.find('\0') != string::npos) {
        result.error = "SQL contains NUL";
    }
    if (result.error.empty()) {
        sqlite3_progress_handler(db->connection, 1000, database_progress, db);
        const char *tail = nullptr;
        int code = sqlite3_prepare_v3(db->connection, sql.c_str(), (int)sql.length() + 1, 0,
                                      &statement, &tail);
        if (code != SQLITE_OK) {
            result.error = sqlite3_errmsg(db->connection);
        } else if (statement == nullptr) {
            result.error = "Empty SQL";
        } else if (tail != nullptr) {
            code = sqlite3_prepare_v3(db->connection, tail, -1, 0, &extra, nullptr);
            if (code != SQLITE_OK || extra != nullptr) {
                result.error = "Multiple SQL statements are forbidden";
            }
        }
    }
    if (result.error.empty() && !sqlite3_stmt_readonly(statement)) {
        result.error = "Only read-only SQL is allowed";
    }
    if (result.error.empty()) {
        result.column_count = sqlite3_column_count(statement);
        if (result.column_count < 1 || result.column_count > 128) {
            result.error = "Expected a result-producing query";
        }
        for (int c = 0; c < result.column_count; c++) {
            result.columns[c] = sqlite3_column_name(statement, c);
        }
    }
    if (row_limit < 0) {
        row_limit = options->max_display_rows;
    }
    if (row_limit < 1) {
        result.error = "Invalid row limit";
    }
    if (result.error.empty()) {
        // 仅一次分配行索引。单元格按实际读取的行分配，避免预留巨量内存。
        result.rows = new (nothrow) QueryRow[row_limit];
        if (result.rows == nullptr) {
            result.error = "Cannot allocate query results";
        }
    }
    int bytes = 0;
    while (result.error.empty()) {
        if (now_ms() >= db->deadline) {
            db->timed_out = true;
            result.error = "Query timed out";
            break;
        }
        if (cancelled) {
            result.error = "Query cancelled";
            break;
        }
        int code = sqlite3_step(statement);
        if (code == SQLITE_DONE) {
            result.success = true;
            break;
        }
        if (code != SQLITE_ROW) {
            result.error = sqlite3_errmsg(db->connection);
            break;
        }
        if (result.row_count == row_limit) {
            result.truncated = true;
            result.success = true;
            break;
        }
        Cell *cells = new (nothrow) Cell[result.column_count];
        if (cells == nullptr) {
            result.error = "Cannot allocate query row";
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
        if (!result.error.empty()) {
            delete[] cells;
            break;
        }
        if (bytes + row_bytes > options->max_result_bytes) {
            delete[] cells;
            result.truncated = true;
            result.success = true;
            break;
        }
        QueryRow *row = &result.rows[result.row_count];
        row->cells = cells;
        result.row_count++;
        bytes += row_bytes;
    }
    if (now_ms() >= db->deadline) {
        db->timed_out = true;
        result.error = "Query timed out";
        result.success = false;
    }
    if (db->timed_out) {
        result.error = "Query timed out";
    }
    if (cancelled) {
        result.error = "Query cancelled";
        result.success = false;
    }
    sqlite3_finalize(extra);
    sqlite3_finalize(statement);
    if (db->connection != nullptr) {
        sqlite3_progress_handler(db->connection, 0, nullptr, nullptr);
    }
    result.timed_out = db->timed_out;
    result.cancelled = cancelled != 0;
    if (!result.success) {
        free_query(&result);
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
        result.error = "Schema exceeds size limit";
        free_query(&result);
    }
    return result;
}
QueryResult list_tables(Database *db) {
    return internal_query(db, "SELECT name, type FROM sqlite_schema WHERE type IN ('table','view') "
                              "AND name NOT GLOB 'sqlite_*' ORDER BY name");
}

TextResult read_schema(Database *db) {
    TextResult result;
    QueryResult tables = list_tables(db);
    if (!tables.success) {
        return failed_text(tables.error);
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
            result.error = "数据库结构超过大小上限";
        }
        if (!result.error.empty()) {
            break;
        }
    }
    free_query(&tables);
    result.success = result.error.empty();
    return result;
}
