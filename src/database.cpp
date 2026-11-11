// SQLite 的调用集中在这里，单元格用一块动态数组保存。
void free_query(QueryResult *result) {
    delete[] result->cells;
    result->cells = nullptr;
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
    if (action == SQLITE_SELECT || action == SQLITE_READ || action == SQLITE_RECURSIVE) {
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
        int count = sizeof(allowed) / sizeof(allowed[0]);
        for (int i = 0; i < count; i++) {
            if (sqlite3_stricmp(arg2, allowed[i]) == 0) {
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
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db->connection, SQLITE_DBCONFIG_DQS_DML, 0, nullptr);
    if (sqlite3_exec(db->connection, "PRAGMA query_only=ON", nullptr, nullptr, nullptr) !=
        SQLITE_OK) {
        return failed_status("数据库");
    }
    sqlite3_busy_timeout(db->connection, sql_timeout_ms);
    // 限制 SQL、结果大小和执行复杂度，禁止附加数据库。
    sqlite3_limit(db->connection, SQLITE_LIMIT_LENGTH, 1048576);
    sqlite3_limit(db->connection, SQLITE_LIMIT_SQL_LENGTH, sql_limit);
    sqlite3_limit(db->connection, SQLITE_LIMIT_COLUMN, query_column_limit);
    sqlite3_limit(db->connection, SQLITE_LIMIT_EXPR_DEPTH, 100);
    sqlite3_limit(db->connection, SQLITE_LIMIT_COMPOUND_SELECT, 20);
    sqlite3_limit(db->connection, SQLITE_LIMIT_VDBE_OP, 250000);
    sqlite3_limit(db->connection, SQLITE_LIMIT_ATTACHED, 0);
    sqlite3_set_authorizer(db->connection, database_authorize, db);
    return ok_status();
}
// 启动时直接打开并验证唯一的只读连接。
Status open_database(Database *db, string path) {
    assert(db->connection == nullptr);
    if (path.empty() || path == ":memory:" || path.find('\0') != string::npos) {
        return failed_status("数据库");
    }
    int code = sqlite3_open_v2(path.c_str(), &db->connection,
                               SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
    Status state;
    if (code == SQLITE_OK) {
        state = configure_database(db);
        if (state.success) {
            QueryResult tables = list_tables(db);
            state.success = tables.success;
            free_query(&tables);
        }
    }
    if (state.success == false) {
        close_database(db);
        return failed_status("数据库");
    }
    return ok_status();
}
// 在数据库边界统一转成文本，保留 NULL 和既有数字格式。
Cell read_cell(sqlite3_stmt *statement, int column) {
    Cell result;
    int type = sqlite3_column_type(statement, column);
    result.is_null = type == SQLITE_NULL;
    if (type == SQLITE_INTEGER) {
        result.text = to_string(sqlite3_column_int64(statement, column));
    } else if (type == SQLITE_FLOAT) {
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%.17g", sqlite3_column_double(statement, column));
        result.text = buffer;
    } else if (type == SQLITE_TEXT) {
        const char *text = (const char *)sqlite3_column_text(statement, column);
        int bytes = sqlite3_column_bytes(statement, column);
        if (text != nullptr) {
            result.text.assign(text, bytes);
        }
    } else if (type == SQLITE_BLOB) {
        result.text = "0x";
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
    assert(db->connection != nullptr);
    QueryResult result;
    result.sql = sql;
    db->deadline = now_ms() + sql_timeout_ms;
    db->timed_out = false;
    sqlite3_stmt *statement = nullptr;
    sqlite3_stmt *extra = nullptr;
    // 先校验输入和读取上限，再交给 SQLite 编译检查。
    bool ready = !cancelled && (int)sql.length() <= sql_limit &&
                 sql.find('\0') == string::npos;
    if (row_limit < 0) {
        row_limit = query_row_limit;
    }
    assert(row_limit >= 1 && row_limit <= 100000);
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
            if (result.column_count < 1 || result.column_count > query_column_limit) {
                ready = false;
            }
        }
    }
    if (ready) {
        for (int c = 0; c < result.column_count; c++) {
            result.columns[c] = sqlite3_column_name(statement, c);
            if (!db->internal) {
                result.columns[c] = limit_text(result.columns[c], query_name_limit,
                                                &result.truncated);
            }
        }
        result.cells = new (nothrow) Cell[row_limit * result.column_count];
        ready = result.cells != nullptr;
    }
    // 普通结果为列名和 JSON 元数据计入容量；内部查询维持原容量。
    int bytes = db->internal ? 0 : (int)columns_json(&result).length() + 128;
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
        Cell *cells = result.cells + result.row_count * result.column_count;
        for (int c = 0; c < result.column_count; c++) {
            cells[c] = read_cell(statement, c);
            if (!db->internal && !cells[c].is_null) {
                cells[c].text = limit_text(cells[c].text, query_value_limit, &result.truncated);
            }
        }
        int row_bytes = (int)row_json(cells, result.column_count).length() + 1;
        if (bytes + row_bytes > query_result_limit) {
            result.success = true;
            result.truncated = true;
            break;
        }
        result.row_count++;
        bytes += row_bytes;
    }
    if (now_ms() >= db->deadline) {
        db->timed_out = true;
    }
    if (db->timed_out || cancelled) {
        result.success = false;
    }
    // 所有执行分支统一释放语句、移除回调，并清理失败结果。
    sqlite3_finalize(extra);
    sqlite3_finalize(statement);
    sqlite3_progress_handler(db->connection, 0, nullptr, nullptr);
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
    db->internal = true;
    QueryResult result = execute_sql(db, sql, 10000);
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
        Cell *table = tables.cells + i * tables.column_count;
        string name = table[0].text;
        result.text += "表 " + quote_name(name) + " (" + table[1].text + ")\n";
        QueryResult columns = internal_query(db, "PRAGMA table_xinfo(" + quote_name(name) + ")");
        if (!columns.success) {
            result.error = columns.error;
        }
        for (int r = 0; r < columns.row_count; r++) {
            Cell *column = columns.cells + r * columns.column_count;
            result.text += "  " + quote_name(column[1].text) + " " + column[2].text;
            if (column[3].text == "1") {
                result.text += " NOT NULL";
            }
            if (column[5].text != "0") {
                result.text += " PRIMARY KEY " + column[5].text;
            }
            result.text += "\n";
        }
        free_query(&columns);
        QueryResult keys = internal_query(db, "PRAGMA foreign_key_list(" + quote_name(name) + ")");
        if (!keys.success) {
            result.error = keys.error;
        }
        for (int r = 0; r < keys.row_count; r++) {
            Cell *key = keys.cells + r * keys.column_count;
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
