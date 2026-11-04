// Benchmark 用普通结构体保存结果，用指针比较行数据。
bool numeric_cell(Cell *cell) {
    return cell->type == SQLITE_INTEGER || cell->type == SQLITE_FLOAT;
}
long double cell_number(Cell *cell) {
    if (cell->type == SQLITE_INTEGER) {
        return cell->integer;
    }
    return cell->real;
}
bool cell_equal(Cell *a, Cell *b) {
    // 整数和浮点数可以互相比较，允许少量浮点计算误差。
    if (numeric_cell(a) && numeric_cell(b)) {
        long double x = cell_number(a);
        long double y = cell_number(b);
        if (x == y) {
            return true;
        }
        if (!isfinite(x) || !isfinite(y)) {
            return false;
        }
        long double scale = 1;
        if (abs(x) > scale) {
            scale = abs(x);
        }
        if (abs(y) > scale) {
            scale = abs(y);
        }
        return abs(x - y) <= 1e-9L * scale;
    }
    if (a->type != b->type) {
        return false;
    }
    if (a->type == SQLITE_NULL) {
        return true;
    }
    return a->text == b->text;
}
// 排序按类型和值确定顺序，浮点容差仅用于相等判断。
bool less_cell(Cell *a, Cell *b) {
    if (numeric_cell(a) && numeric_cell(b)) {
        return cell_number(a) < cell_number(b);
    }
    if (a->type != b->type) {
        return a->type < b->type;
    }
    return a->text < b->text;
}
bool less_row(QueryResult *result, int a, int b) {
    for (int c = 0; c < result->column_count; c++) {
        QueryRow *left_row = &result->rows[a];
        QueryRow *right_row = &result->rows[b];
        Cell *left = &left_row->cells[c];
        Cell *right = &right_row->cells[c];
        if (less_cell(left, right)) {
            return true;
        }
        if (less_cell(right, left)) {
            return false;
        }
    }
    return false;
}
// 单线程评测只排序行号，借用当前结果指针，不复制单元格。
QueryResult *sorting_query = nullptr;
int compare_row_indices(const void *a, const void *b) {
    int left = *(const int *)a;
    int right = *(const int *)b;
    if (less_row(sorting_query, left, right)) {
        return -1;
    }
    if (less_row(sorting_query, right, left)) {
        return 1;
    }
    return 0;
}
void sort_row_indices(QueryResult *result, int *indices) {
    for (int i = 0; i < result->row_count; i++) {
        indices[i] = i;
    }
    sorting_query = result;
    qsort(indices, result->row_count, sizeof(int), compare_row_indices);
    sorting_query = nullptr;
}
// 按列位置比较；有外层 ORDER BY 时保留行顺序，否则先排序行号。
bool equivalent_results(QueryResult *prediction, QueryResult *gold, bool ordered) {
    // 只比较完整且形状一致的结果，截断不能算作匹配。
    if (prediction->success == false) {
        return false;
    }
    if (gold->success == false) {
        return false;
    }
    if (prediction->truncated) {
        return false;
    }
    if (gold->truncated) {
        return false;
    }
    if (prediction->column_count != gold->column_count) {
        return false;
    }
    if (prediction->row_count != gold->row_count) {
        return false;
    }
    int count = prediction->row_count;
    if (count == 0) {
        return true;
    }
    // 用两组行号对应待比较数据，保持原结果不变。
    int *left = new (nothrow) int[count];
    int *right = new (nothrow) int[count];
    bool allocation_failed = false;
    if (left == nullptr) {
        allocation_failed = true;
    }
    if (right == nullptr) {
        allocation_failed = true;
    }
    if (allocation_failed) {
        delete[] left;
        delete[] right;
        return false;
    }
    if (ordered) {
        for (int i = 0; i < count; i++) {
            left[i] = i;
            right[i] = i;
        }
    } else {
        // 排序行号仍保留重复行，避免按集合比较丢失数量。
        sort_row_indices(prediction, left);
        sort_row_indices(gold, right);
    }
    bool equal = true;
    for (int r = 0; r < count; r++) {
        if (equal == false) {
            break;
        }
        for (int c = 0; c < prediction->column_count; c++) {
            QueryRow *predicted_row = &prediction->rows[left[r]];
            QueryRow *reference_row = &gold->rows[right[r]];
            Cell *predicted_cell = &predicted_row->cells[c];
            Cell *reference_cell = &reference_row->cells[c];
            if (cell_equal(predicted_cell, reference_cell) == false) {
                equal = false;
                break;
            }
        }
    }
    delete[] left;
    delete[] right;
    return equal;
}
struct OrderState {
    int depth = 0;
    char quote = 0;
    string token;
    string last;
};
// 只在括号外匹配连续的 ORDER、BY 标记。
bool finish_order_token(OrderState *state) {
    if (state->token.empty()) {
        return false;
    }
    bool found = state->depth == 0 && state->last == "ORDER" && state->token == "BY";
    if (state->depth == 0) {
        state->last = state->token;
    } else {
        state->last.clear();
    }
    state->token.clear();
    return found;
}
bool is_sql_quote(char value) {
    if (value == '\'') {
        return true;
    }
    if (value == '"') {
        return true;
    }
    if (value == '`') {
        return true;
    }
    if (value == '[') {
        return true;
    }
    return false;
}
// 跳过引号、注释和括号内排序，只识别最外层 ORDER BY。
bool has_outer_order_by(string sql) {
    OrderState state;
    for (int i = 0; i < (int)sql.length(); i++) {
        char c = sql[i];
        char next = '\0';
        if (i + 1 < (int)sql.length()) {
            next = sql[i + 1];
        }
        bool line_comment = c == '-' && next == '-';
        bool block_comment = c == '/' && next == '*';
        // 引号内文本和注释不参与排序关键词判断。
        if (state.quote != 0) {
            if (c == state.quote) {
                if (i + 1 < (int)sql.length() && sql[i + 1] == state.quote) {
                    i++;
                } else {
                    state.quote = 0;
                }
            }
            continue;
        }
        if (is_sql_quote(c)) {
            if (finish_order_token(&state)) {
                return true;
            }
            state.quote = c;
            if (c == '[') {
                state.quote = ']';
            }
            state.last.clear();
        } else if (line_comment) {
            if (finish_order_token(&state)) {
                return true;
            }
            while (i < (int)sql.length() && sql[i] != '\n') {
                i++;
            }
        } else if (block_comment) {
            if (finish_order_token(&state)) {
                return true;
            }
            i += 2;
            while (i + 1 < (int)sql.length()) {
                if (sql[i] == '*') {
                    if (sql[i + 1] == '/') {
                        break;
                    }
                }
                i++;
            }
            i++;
        } else if (isalnum((unsigned char)c) || c == '_') {
            state.token += (char)toupper((unsigned char)c);
        } else {
            if (finish_order_token(&state)) {
                return true;
            }
            if (c == '(') {
                state.depth++;
                state.last.clear();
            } else if (c == ')') {
                state.depth--;
                state.last.clear();
            } else if (!isspace((unsigned char)c)) {
                state.last.clear();
            }
        }
    }
    return finish_order_token(&state);
}

// CSV 字段统一加引号，内部双引号重复转义。
string csv_field(string text) {
    string result = "\"";
    for (int i = 0; i < (int)text.length(); i++) {
        result += text[i];
        if (text[i] == '"') {
            result += '"';
        }
    }
    return result + '"';
}
// 每题生成一次 SQL，不请求分析，在本地比较标准答案并写入 CSV。
BenchmarkResult run_benchmark(BenchmarkOptions *options, Config *config) {
    BenchmarkResult result;
    if (options->limit < 1 || options->limit > 100000) {
        result.error = error_message("数据集");
        return result;
    }
    // 先准备数据集和输出文件，失败时不发起模型请求。
    DatasetResult dataset = load_dataset(options);
    if (dataset.success == false) {
        result.error = dataset.error;
        return result;
    }
    result.output = options->output;
    if (result.output.empty()) {
        result.output = "results/benchmark-" + to_string(time(nullptr)) + "-" +
                        to_string((long long)now_ms()) + ".csv";
    }
    OutputFile output;
    Status state = open_output(&output, result.output);
    if (state.success) {
        state = write_output(&output, "question,db_id,sql,executed,matched,error\n");
    }
    if (state.success == false) {
        result.error = state.error;
        delete[] dataset.items;
        return result;
    }
    bool incomplete = false;
    for (int i = 0; i < dataset.count; i++) {
        if (cancelled || result.error.empty() == false) {
            break;
        }
        DatasetItem *item = &dataset.items[i];
        BenchmarkStats *stats = &result.stats;
        // 失败题也计入分母，统计实际尝试的题数。
        stats->total++;
        cout << "评测 " << i + 1 << "/" << dataset.count << "：" << item->db_id << "\n";
        // 每题独立创建预测和参考连接，避免配置相互影响。
        Database db;
        Database reference;
        QueryRun run;
        QueryResult gold;
        string error;
        string match;
        TextResult path = dataset_database(dataset.root, item->db_id);
        DatabaseOptions limits = config->database;
        limits.max_display_rows = 100000;
        limits.max_result_bytes = 16 * 1024 * 1024;
        Status opened;
        if (path.success) {
            opened = open_database(&db, path.text, limits);
        } else {
            opened = failed_status("数据集");
        }
        if (opened.success) {
            Completion source;
            source.api = config->api;
            run = run_query(&db, item->question, &source, false);
            error = run.error;
            if (run.api_error) {
                result.error = run.error;
            }
            if (run.success) {
                stats->executed++;
                // 仅标准答案放宽双引号字符串规则。
                limits.reference_double_quoted_strings = true;
                opened = open_database(&reference, path.text, limits);
                if (opened.success) {
                    // 标准 SQL 只在本地执行用于比较，不放进模型提示词。
                    gold = execute_sql(&reference, item->query);
                    QueryResult *query = &run.query;
                    if (gold.success == false || gold.truncated || query->truncated) {
                        error = error_message("数据集");
                    } else {
                        // 空值表示未完成比较，0 表示已比较但不一致。
                        match = "0";
                        if (equivalent_results(query, &gold, has_outer_order_by(item->query))) {
                            match = "1";
                            stats->matched++;
                        }
                    }
                }
            }
        }
        if (opened.success == false) {
            error = opened.error;
            incomplete = true;
        }
        if (run.success && match.empty()) {
            incomplete = true;
        }
        string executed = "0";
        if (run.success) {
            executed = "1";
        }
        QueryResult *query = &run.query;
        // 每题立即写出状态，保留评测中途停止前的结果。
        string row = csv_field(item->question) + "," + csv_field(item->db_id) + "," +
                     csv_field(query->sql) + "," + executed + "," + match + "," + csv_field(error) +
                     "\n";
        state = write_output(&output, row);
        if (state.success == false) {
            result.error = state.error;
        }
        free_query(&run.query);
        free_query(&gold);
        close_database(&db);
        close_database(&reference);
    }
    delete[] dataset.items;
    // 区分预测不匹配与参考查询或资源失败导致的评测未完成。
    if (incomplete && result.error.empty()) {
        result.error = error_message("数据集");
    }
    if (cancelled) {
        result.error = error_message("数据集");
    }
    result.success = result.error.empty();
    return result;
}
