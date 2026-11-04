// 文件系统细节集中在这里。业务模块只传递和拼接普通字符串。
namespace fs = std::filesystem;

string join_path(string folder, string name) {
    if (folder.empty()) {
        return name;
    }
    char last = folder.back();
    if (last == '/') {
        return folder + name;
    }
    if (last == '\\') {
        return folder + name;
    }
    return folder + "/" + name;
}
string parent_path(string path) {
    fs::path native = fs::u8path(path);
    fs::path parent = native.parent_path();
    return parent.u8string();
}
// 配置中的相对路径以配置文件目录为基准。
string relative_path(string config_file, string path) {
    fs::path native = fs::u8path(path);
    if (native.is_absolute()) {
        return path;
    }
    string folder = parent_path(config_file);
    fs::path base = fs::u8path(folder);
    fs::path resolved = base / native;
    return resolved.u8string();
}
bool file_exists(string path) {
    error_code error;
    fs::path native = fs::u8path(path);
    return fs::exists(native, error);
}
// 获取已存在路径的绝对形式，并解析符号链接。
TextResult canonical_path(string path) {
    error_code error;
    fs::path native = fs::u8path(path);
    fs::path resolved = fs::canonical(native, error);
    if (error) {
        return failed_text("文件");
    }
    TextResult result;
    result.success = true;
    result.text = resolved.u8string();
    return result;
}
// 检查路径是否仍在指定根目录内。
// 调用前须解析真实路径，防止符号链接绕过目录边界。
bool path_inside(string path, string root) {
    fs::path native = fs::u8path(path);
    fs::path base = fs::u8path(root);
    fs::path relative = native.lexically_relative(base);
    if (relative.empty()) {
        return false;
    }
    if (relative.is_absolute()) {
        return false;
    }
    string text = relative.generic_u8string();
    if (text == "..") {
        return false;
    }
    if (text.compare(0, 3, "../") == 0) {
        return false;
    }
    return true;
}
// 按容量上限读取文本，完整读到文件末尾才返回成功。
TextResult read_text(string path, int max_bytes) {
    fs::path native = fs::u8path(path);
    ifstream input(native, ios::binary);
    if (input.is_open() == false) {
        return failed_text("文件");
    }
    TextResult result;
    char buffer[4096];
    while (input) {
        input.read(buffer, sizeof(buffer));
        int count = (int)input.gcount();
        int total = (int)result.text.length() + count;
        if (total > max_bytes) {
            return failed_text("文件");
        }
        result.text.append(buffer, count);
    }
    if (input.eof() == false) {
        return failed_text("文件");
    }
    result.success = true;
    return result;
}
// 创建输出目录并打开新文件，已有文件则返回错误。
Status open_output(OutputFile *output, string path) {
    error_code error;
    fs::path native = fs::u8path(path);
    string folder = parent_path(path);
    if (folder.empty() == false) {
        fs::path parent = fs::u8path(folder);
        fs::create_directories(parent, error);
        if (error) {
            return failed_status("文件");
        }
    }
    bool exists = fs::exists(native, error);
    if (error) {
        return failed_status("文件");
    }
    if (exists) {
        // 拒绝已有文件，避免覆盖历史评测结果。
        return failed_status("文件");
    }
    output->stream.open(native, ios::binary);
    if (output->stream.is_open() == false) {
        return failed_status("文件");
    }
    return ok_status();
}
// 每次写入后刷新输出流，及时发现文件写入失败。
Status write_output(OutputFile *output, string text) {
    output->stream.write(text.data(), (streamsize)text.length());
    output->stream.flush();
    if (output->stream.fail()) {
        return failed_status("文件");
    }
    return ok_status();
}
// 按 Spider 目录布局定位数据库，并校验目录边界。
TextResult dataset_database(string root, string id) {
    // 数据库名称只能是简单名称，解析后的路径也必须留在数据集目录内。
    if (id.empty()) {
        return failed_text("文件");
    }
    if (id.length() > 128) {
        return failed_text("文件");
    }
    if (id == ".") {
        return failed_text("文件");
    }
    if (id == "..") {
        return failed_text("文件");
    }
    for (int i = 0; i < (int)id.length(); i++) {
        unsigned char c = id[i];
        if (isalnum(c)) {
            continue;
        }
        if (c == '_') {
            continue;
        }
        if (c == '-') {
            continue;
        }
        return failed_text("文件");
    }
    string directory = join_path(root, "database");
    directory = join_path(directory, id);
    string path = join_path(directory, id + ".sqlite");
    TextResult resolved = canonical_path(path);
    if (resolved.success == false) {
        return failed_text("文件");
    }
    if (path_inside(resolved.text, root) == false) {
        return failed_text("文件");
    }
    return resolved;
}
// 加载指定划分，返回本次评测所需的题目数组。
DatasetResult load_dataset(BenchmarkOptions *options) {
    // 只复制前 limit 道题；items 由调用者 delete[]。
    DatasetResult result;
    result.error = error_message("数据集");
    result.split = options->split;
    if (result.split == "train") {
        result.split = "train_spider";
    }
    string splits[] = {"dev", "train_spider", "train_others", "test"};
    bool supported = false;
    for (int i = 0; i < 4; i++) {
        if (result.split == splits[i]) {
            supported = true;
        }
    }
    if (supported == false) {
        return result;
    }
    // 校验数据集和 JSON 的真实路径，防止读取目录外文件。
    TextResult root = canonical_path(options->dataset);
    if (root.success == false) {
        return result;
    }
    result.root = root.text;
    TextResult path = canonical_path(join_path(result.root, result.split + ".json"));
    if (path.success == false || path_inside(path.text, result.root) == false) {
        return result;
    }
    TextResult text = read_text(path.text, 64 * 1024 * 1024);
    cJSON *array = json_parse(text.text);
    if (text.success == false || json_is_array(array) == false) {
        free_json(array);
        return result;
    }
    int count = json_size(array);
    if (count > options->limit) {
        count = options->limit;
    }
    if (count < 1) {
        free_json(array);
        return result;
    }
    result.items = new (nothrow) DatasetItem[count];
    if (result.items == nullptr) {
        free_json(array);
        return result;
    }
    result.count = count;
    cJSON *item = json_first_item(array);
    bool valid = true;
    // 复制必要字段，题目数组不依赖随后释放的 JSON 树。
    for (int i = 0; i < count; i++) {
        TextResult id = read_json_string(item, "db_id");
        TextResult question = read_json_string(item, "question");
        TextResult query = read_json_string(item, "query");
        valid = id.success && question.success && query.success;
        if (valid == false) {
            break;
        }
        DatasetItem *destination = &result.items[i];
        destination->db_id = id.text;
        destination->question = question.text;
        destination->query = query.text;
        item = json_next_item(item);
    }
    free_json(array);
    if (valid == false) {
        delete[] result.items;
        result.items = nullptr;
        result.count = 0;
        return result;
    }
    result.success = true;
    result.error.clear();
    return result;
}
