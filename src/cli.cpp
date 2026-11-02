// 启动后直接输入自然语言，数据库在启动时连接一次。
void print_error(string error) {
    cerr << "错误：" << error << '\n';
}
void print_help() {
    cout << "用法：text2sql [--config 配置文件] [--db SQLite文件]\n"
         << "      text2sql [--config 配置文件] benchmark\n"
         << "默认直接输入自然语言；/help 查看引导，/exit 退出。\n";
}
Args parse_arguments(int argc, char **argv) {
    Args args;
    args.error = error_message("参数");
    for (int i = 1; i < argc; i++) {
        string value = argv[i];
        if (value == "--help" || value == "-h") {
            args.help = true;
        } else if (value == "benchmark") {
            if (args.benchmark) {
                return args;
            }
            args.benchmark = true;
        } else if (value == "--db") {
            if (args.db_given) {
                return args;
            }
            if (i + 1 == argc) {
                return args;
            }
            i++;
            args.db = argv[i];
            args.db_given = true;
        } else if (value == "--config") {
            if (args.config_required) {
                return args;
            }
            if (i + 1 == argc) {
                return args;
            }
            i++;
            args.config = argv[i];
            args.config_required = true;
        } else {
            return args;
        }
    }
    if (args.db_given && args.benchmark) {
        return args;
    }
    args.success = true;
    args.error.clear();
    return args;
}
bool parse_positive_int(string text, int *number, int maximum) {
    if (text.empty()) {
        return false;
    }
    int value = 0;
    for (int i = 0; i < (int)text.length(); i++) {
        if (text[i] < '0') {
            return false;
        }
        if (text[i] > '9') {
            return false;
        }
        int digit = text[i] - '0';
        if (value > (maximum - digit) / 10) {
            return false;
        }
        value = value * 10 + digit;
    }
    if (value < 1) {
        return false;
    }
    if (value > maximum) {
        return false;
    }
    *number = value;
    return true;
}
string ask_text(string label) {
    cout << label;
    string text;
    getline(cin, text);
    return trim(text);
}
string ask_path(string label) {
    string path = ask_text(label);
    if (path.length() > 1) {
        bool double_quoted = path.front() == '"' && path.back() == '"';
        bool single_quoted = path.front() == '\'' && path.back() == '\'';
        if (double_quoted || single_quoted) {
            path = path.substr(1, path.length() - 2);
        }
    }
    return path;
}
string display_text(string text) {
    for (int i = 0; i < (int)text.length(); i++) {
        unsigned char c = text[i];
        if (c < 32) {
            text[i] = ' ';
        }
        if (c == 127) {
            text[i] = ' ';
        }
    }
    if (text.length() > 160) {
        int end = 160;
        while (end > 0) {
            if (is_utf8_tail((unsigned char)text[end]) == false) {
                break;
            }
            end--;
        }
        text = text.substr(0, end) + "...";
    }
    return text;
}
void print_table(QueryResult *result) {
    for (int c = 0; c < result->column_count; c++) {
        cout << display_text(result->columns[c]) << "  ";
    }
    cout << '\n';
    for (int r = 0; r < result->row_count; r++) {
        QueryRow *row = &result->rows[r];
        for (int c = 0; c < result->column_count; c++) {
            Cell *cell = &row->cells[c];
            cout << display_text(cell_text(cell)) << "  ";
        }
        cout << '\n';
    }
    cout << result->row_count << " 行";
    if (result->truncated) {
        cout << "（结果已截断）";
    }
    cout << '\n';
}
void print_analysis_text(string text) {
    for (int i = 0; i < (int)text.length(); i++) {
        unsigned char c = text[i];
        if (c == '\n' || c == '\t') {
            cout << text[i];
        } else if (c < 32 || c == 127) {
            cout << ' ';
        } else {
            cout << text[i];
        }
    }
    cout << '\n';
}
void print_result(QueryRun *result) {
    // 分开显示 SQL、执行结果和分析；分析失败不会遮住已经查询到的数据。
    if (result->invalid_input) {
        cout << invalid_query_hint << '\n';
        return;
    }
    QueryResult *query = &result->query;
    if (query->sql.empty()) {
        print_error(result->error);
        return;
    }
    cout << "\nSQL 语句\n--------------------------------\n" << query->sql << '\n';
    cout << "\n查询结果\n--------------------------------\n";
    if (result->success) {
        print_table(query);
    } else {
        cout << flush;
        print_error(result->error);
    }
    cout << "--------------------------------\n";
    if (result->success) {
        TextResult *analysis = &result->analysis;
        cout << "\n分析结果\n--------------------------------\n";
        if (analysis->success) {
            print_analysis_text(analysis->text);
        } else {
            cout << "分析暂不可用：" << analysis->error << '\n';
        }
        cout << "--------------------------------\n";
    }
}
void print_stats(string name, BenchmarkStats *stats) {
    double executed = 0;
    double matched = 0;
    if (stats->total > 0) {
        executed = 100.0 * stats->executed / stats->total;
        matched = 100.0 * stats->matched / stats->total;
    }
    cout << name << "：" << stats->total << " 题，执行成功率 " << executed << "% ，结果一致率 "
         << matched << "%\n";
}
int run_benchmark_prompt(Config *config) {
    ApiOptions *api = &config->api;
    if (api->api_key.empty()) {
        print_error("请先配置 API Key");
        return 1;
    }
    BenchmarkOptions options;
    options.dataset = ask_path("Spider 数据集目录 [内置数据集]：");
    if (options.dataset.empty()) {
        options.dataset = join_path(config->bundled_data, "spider");
    }
    string split = ask_text("数据划分 [dev]：");
    if (!split.empty()) {
        options.split = split;
    }
    string count = ask_text("评测题数 [20]：");
    if (count.empty() == false) {
        if (parse_positive_int(count, &options.limit, 100000) == false) {
            print_error(error_message("参数"));
            return 2;
        }
    }
    options.output = ask_path("CSV 路径 [自动生成]：");
    if (!cin) {
        return 0;
    }
    BenchmarkResult result = run_benchmark(&options, config);
    if (!result.success) {
        print_error(result.error);
    }
    print_stats("单次 SQL 评测", &result.stats);
    if (!result.output.empty()) {
        cout << "CSV：" << result.output << '\n';
    }
    if (cancelled) {
        return 130;
    }
    if (result.success) {
        return 0;
    }
    return 1;
}
void print_guide(string table) {
    cout << "直接输入问题并按回车，每次查询后可以继续提问。\n";
    if (!table.empty()) {
        cout << "例如：查询「" << display_text(table) << "」表的记录总数。\n";
    }
    cout << "输入 /help 查看引导，/exit 退出。\n\n";
}
int run_chat(Config *config, string path) {
    // 整个会话共用一个数据库连接，每次提问重新建立模型上下文。
    ApiOptions *api = &config->api;
    cout << "SQLite 自然语言查询\n";
    if (path.empty()) {
        path = ask_path("首次使用，请输入 SQLite 数据库路径：");
    }
    if (!cin) {
        return 0;
    }
    Database db;
    Status opened = open_database(&db, path, config->database);
    if (!opened.success) {
        print_error(opened.error);
        return 1;
    }
    QueryResult tables = list_tables(&db);
    string example;
    if (tables.success) {
        if (tables.row_count > 0) {
            QueryRow *row = &tables.rows[0];
            Cell *table = &row->cells[0];
            example = table->text;
        }
    }
    free_query(&tables);
    cout << "已连接：" << path << "\n";
    print_guide(example);
    if (api->api_key.empty()) {
        cout << "请在 JSON 配置或 DEEPSEEK_API_KEY 中配置自己的 AI 密钥。\n";
    }
    while (!cancelled) {
        string question = ask_text("你：");
        if (!cin) {
            break;
        }
        if (question == "/exit") {
            break;
        }
        if (question.empty()) {
            continue;
        }
        if (question == "/help") {
            print_guide(example);
            continue;
        }
        if (question[0] == '/') {
            cout << invalid_query_hint << '\n';
            continue;
        }
        if (api->api_key.empty()) {
            print_error("请先配置 AI 密钥");
            continue;
        }
        cout << "正在处理…\n";
        Completion source;
        source.api = config->api;
        QueryRun result = run_query(&db, question, &source, true);
        print_result(&result);
        free_query(&result.query);
        cout << '\n';
    }
    close_database(&db);
    if (cancelled) {
        return 130;
    }
    return 0;
}
