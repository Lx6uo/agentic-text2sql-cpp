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
// 只排序行号，不复制字符串。归并排序保持 O(n log n)。
void sort_row_indices(QueryResult *result, int *indices, int *work) {
    int count = result->row_count;
    for (int i = 0; i < count; i++) {
        indices[i] = i;
    }
    for (int width = 1; width < count; width *= 2) {
        for (int start = 0; start < count; start += width * 2) {
            int middle = start + width;
            int end = start + width * 2;
            if (middle > count) {
                middle = count;
            }
            if (end > count) {
                end = count;
            }
            int left = start;
            int right = middle;
            for (int target = start; target < end; target++) {
                if (left == middle) {
                    work[target] = indices[right];
                    right++;
                } else if (right == end) {
                    work[target] = indices[left];
                    left++;
                } else if (less_row(result, indices[right], indices[left])) {
                    work[target] = indices[right];
                    right++;
                } else {
                    work[target] = indices[left];
                    left++;
                }
            }
        }
        for (int i = 0; i < count; i++) {
            indices[i] = work[i];
        }
    }
}
bool equivalent_results(QueryResult *prediction, QueryResult *gold, bool ordered) {
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
    int *left = new (nothrow) int[count];
    int *right = new (nothrow) int[count];
    int *work = new (nothrow) int[count];
    bool allocation_failed = false;
    if (left == nullptr) {
        allocation_failed = true;
    }
    if (right == nullptr) {
        allocation_failed = true;
    }
    if (work == nullptr) {
        allocation_failed = true;
    }
    if (allocation_failed) {
        delete[] left;
        delete[] right;
        delete[] work;
        return false;
    }
    if (ordered) {
        for (int i = 0; i < count; i++) {
            left[i] = i;
            right[i] = i;
        }
    } else {
        sort_row_indices(prediction, left, work);
        sort_row_indices(gold, right, work);
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
    delete[] work;
    return equal;
}
struct OrderState {
    int depth = 0;
    char quote = 0;
    string token;
    string last;
};
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
BenchmarkResult run_benchmark(BenchmarkOptions *options, Config *config) {
    BenchmarkResult result;
    bool invalid_limit = false;
    if (options->limit < 1) {
        invalid_limit = true;
    }
    if (options->limit > 100000) {
        invalid_limit = true;
    }
    if (invalid_limit) {
        result.error = "评测题数需为 1–100000";
        return result;
    }
    DatasetResult dataset = load_dataset(options);
    if (!dataset.success) {
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
        state = write_output(&output, "question,db_id,mode,sql,executed,matched,error\n");
    }
    if (!state.success) {
        result.error = state.error;
        delete[] dataset.items;
        return result;
    }
    bool incomplete = false;
    for (int i = 0; i < dataset.count; i++) {
        if (cancelled) {
            break;
        }
        if (result.error.empty() == false) {
            break;
        }
        DatasetItem *item = &dataset.items[i];
        cout << "评测 " << i + 1 << "/" << dataset.count << "：" << item->db_id << "\n";
        for (int mode = 0; mode < 2; mode++) {
            if (cancelled) {
                break;
            }
            if (result.error.empty() == false) {
                break;
            }
            string name = "baseline";
            int attempts = 1;
            BenchmarkStats *stats = &result.baseline;
            if (mode == 1) {
                name = "corrected";
                attempts = query_limit(&config->agent);
                stats = &result.corrected;
            }
            stats->total++;
            Database db;
            Database evaluation;
            Database reference;
            QueryRun run;
            QueryResult *query = &run.query;
            QueryResult gold;
            QueryResult prediction;
            string error;
            string match;
            TextResult path = dataset_database(dataset.root, item->db_id);
            Status opened;
            if (!path.success) {
                opened = failed_status(path.error);
            } else {
                opened = open_database(&db, path.text, config->database);
            }
            if (!opened.success) {
                error = opened.error;
            } else {
                Completion source;
                source.api = config->api;
                run = run_query(&db, item->question, &source, attempts);
                error = run.error;
                if (run.success) {
                    stats->executed++;
                }
                if (run.api_error) {
                    result.error = run.error;
                }
                if (run.success) {
                    DatabaseOptions limits = config->database;
                    limits.max_display_rows = 100000;
                    limits.max_result_bytes = 16 * 1024 * 1024;
                    opened = open_database(&evaluation, path.text, limits);
                    limits.reference_double_quoted_strings = true;
                    if (opened.success) {
                        opened = open_database(&reference, path.text, limits);
                    }
                    if (!opened.success) {
                        error = opened.error;
                    } else {
                        gold = execute_sql(&reference, item->query);
                        prediction = execute_sql(&evaluation, query->sql);
                        if (!gold.success) {
                            error = "标准 SQL 失败：" + gold.error;
                        } else if (!prediction.success) {
                            error = "评测查询失败：" + prediction.error;
                        } else if (gold.truncated || prediction.truncated) {
                            error = "结果超过评测上限，不参与比较";
                        } else {
                            match = "0";
                            if (equivalent_results(&prediction, &gold,
                                                   has_outer_order_by(item->query))) {
                                match = "1";
                                stats->matched++;
                            }
                        }
                    }
                }
            }
            if (opened.success == false) {
                incomplete = true;
            }
            if (run.success) {
                if (match.empty()) {
                    incomplete = true;
                }
            }
            string executed = "0";
            if (run.success) {
                executed = "1";
            }
            string row = csv_field(item->question) + "," + csv_field(item->db_id) + "," + name +
                         "," + csv_field(query->sql) + "," + executed + "," + match + "," +
                         csv_field(error) + "\n";
            state = write_output(&output, row);
            if (!state.success) {
                result.error = state.error;
            }
            free_query(&run.query);
            free_query(&gold);
            free_query(&prediction);
            close_database(&db);
            close_database(&evaluation);
            close_database(&reference);
        }
    }
    delete[] dataset.items;
    if (incomplete && result.error.empty()) {
        result.error = "部分结果无法评测，请查看 CSV 的错误列";
    }
    if (cancelled) {
        result.error = "评测已取消，CSV 保留已完成的记录";
    }
    result.success = result.error.empty();
    return result;
}
