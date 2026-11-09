// 主流程：读取 JSON 配置，直接进入自然语言查询。
// 各 CPP 按依赖顺序包含，CMake 只编译本文件，避免重复定义。
#include "common.cpp"
#include "json.cpp"
#include "files.cpp"
#include "database.cpp"
#include "config.cpp"
#include "llm.cpp"
#include "agent.cpp"
#include "cli.cpp"

void cancel_request(int) {
    cancelled = 1;
}
string find_bundled_data(string executable) {
    // 依次兼容直接运行、多配置构建目录和复制后的数据位置。
    string folder = parent_path(executable);
    string parent = parent_path(folder);
    string locations[4];
    locations[0] = join_path(folder, "data");
    locations[1] = join_path(parent, "data");
    locations[2] = TEXT2SQL_DATA_DIR;
    locations[3] = "data";
    for (int i = 0; i < 4; i++) {
        string database = join_path(locations[i], "college_2.sqlite");
        if (file_exists(database)) {
            TextResult path = canonical_path(locations[i]);
            if (path.success) {
                return path.text;
            }
        }
    }
    return "";
}
// 统一处理参数和配置，再进入自然语言查询。
int application_main(int argc, char **argv) {
    signal(SIGINT, cancel_request);
    Args args = parse_arguments(argc, argv);
    if (!args.success) {
        print_error(args.error);
        print_help();
        return 2;
    }
    if (args.help) {
        print_help();
        return 0;
    }
    ConfigResult loaded = load_config(args.config, args.config_required);
    if (!loaded.success) {
        print_error(loaded.error);
        return 2;
    }
    Config *config = &loaded.config;
    config->bundled_data = find_bundled_data(argv[0]);
    // 数据库路径优先级：命令行 > 配置文件 > 内置数据库。
    string path = config->database_path;
    if (args.db_given) {
        path = args.db;
    } else if (path.empty()) {
        if (config->bundled_data.empty() == false) {
            path = join_path(config->bundled_data, "college_2.sqlite");
        }
    }
    return run_chat(config, path);
}
#ifdef _WIN32
TextResult to_utf8(const wchar_t *value) {
    // Windows 命令行使用 UTF-16，转换后才能统一处理中文参数和路径。
    TextResult result;
    int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return failed_text("参数");
    }
    result.text.resize(size);
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, -1, result.text.data(), size,
                            nullptr, nullptr) <= 0) {
        return failed_text("参数");
    }
    result.text.pop_back();
    result.success = true;
    return result;
}
// Windows 入口先将命令行转为 UTF-8，再复用主流程。
int main() {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    int argc = 0;
    LPWSTR *wide = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (wide == nullptr) {
        return 2;
    }
    // values 持有字符串，保证 argv 指针在主流程期间有效。
    string *values = new (nothrow) string[argc];
    char **argv = new (nothrow) char *[argc];
    bool valid = values != nullptr && argv != nullptr;
    for (int i = 0; i < argc && valid; i++) {
        TextResult value = to_utf8(wide[i]);
        if (!value.success) {
            cerr << value.error << '\n';
            valid = false;
        } else {
            values[i] = value.text;
            argv[i] = values[i].data();
        }
    }
    LocalFree(wide);
    int status = 2;
    if (valid) {
        status = application_main(argc, argv);
    }
    delete[] argv;
    delete[] values;
    return status;
}
#else
int main(int argc, char **argv) {
    return application_main(argc, argv);
}
#endif
