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
TextResult canonical_path(string path) {
    error_code error;
    fs::path native = fs::u8path(path);
    fs::path resolved = fs::canonical(native, error);
    if (error) {
        return failed_text("Cannot resolve path: " + path);
    }
    TextResult result;
    result.success = true;
    result.text = resolved.u8string();
    return result;
}
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
TextResult read_text(string path, int max_bytes) {
    error_code error;
    fs::path native = fs::u8path(path);
    uintmax_t bytes = fs::file_size(native, error);
    if (error) {
        return failed_text("Cannot read file: " + path);
    }
    if (bytes > (uintmax_t)max_bytes) {
        return failed_text("File exceeds size limit: " + path);
    }
    ifstream input(native, ios::binary);
    if (input.is_open() == false) {
        return failed_text("Cannot read file: " + path);
    }
    TextResult result;
    char buffer[4096];
    while (input) {
        input.read(buffer, sizeof(buffer));
        int count = (int)input.gcount();
        int total = (int)result.text.length() + count;
        if (total > max_bytes) {
            return failed_text("File exceeds size limit: " + path);
        }
        result.text.append(buffer, count);
    }
    if (input.eof() == false) {
        return failed_text("File read failed: " + path);
    }
    result.success = true;
    return result;
}
Status open_output(OutputFile *output, string path) {
    error_code error;
    fs::path native = fs::u8path(path);
    string folder = parent_path(path);
    if (folder.empty() == false) {
        fs::path parent = fs::u8path(folder);
        fs::create_directories(parent, error);
        if (error) {
            return failed_status("Cannot create output directory");
        }
    }
    bool exists = fs::exists(native, error);
    if (error) {
        return failed_status("Cannot inspect output path");
    }
    if (exists) {
        return failed_status("Cannot create output (existing files are preserved): " + path);
    }
    output->stream.open(native, ios::binary);
    if (output->stream.is_open() == false) {
        return failed_status("Cannot create output: " + path);
    }
    return ok_status();
}
Status write_output(OutputFile *output, string text) {
    output->stream.write(text.data(), (streamsize)text.length());
    output->stream.flush();
    if (output->stream.fail()) {
        return failed_status("Benchmark output write failed");
    }
    return ok_status();
}
TextResult dataset_database(string root, string id) {
    if (id.empty()) {
        return failed_text("Unsafe Spider db_id");
    }
    if (id.length() > 128) {
        return failed_text("Unsafe Spider db_id");
    }
    if (id == ".") {
        return failed_text("Unsafe Spider db_id");
    }
    if (id == "..") {
        return failed_text("Unsafe Spider db_id");
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
        return failed_text("Unsafe Spider db_id");
    }
    string directory = join_path(root, "database");
    directory = join_path(directory, id);
    string path = join_path(directory, id + ".sqlite");
    TextResult resolved = canonical_path(path);
    if (resolved.success == false) {
        return failed_text("Spider database not found: " + id);
    }
    if (path_inside(resolved.text, root) == false) {
        return failed_text("Spider database escapes dataset directory");
    }
    return resolved;
}
DatasetResult load_dataset(BenchmarkOptions *options) {
    DatasetResult result;
    result.split = options->split;
    if (result.split == "train") {
        result.split = "train_spider";
    }
    bool supported = false;
    if (result.split == "dev") {
        supported = true;
    }
    if (result.split == "train_spider") {
        supported = true;
    }
    if (result.split == "train_others") {
        supported = true;
    }
    if (result.split == "test") {
        supported = true;
    }
    if (supported == false) {
        result.error = "Unsupported split: use dev, train_spider, train_others or test";
        return result;
    }
    TextResult root = canonical_path(options->dataset);
    if (root.success == false) {
        result.error = root.error;
        return result;
    }
    result.root = root.text;
    string file = join_path(result.root, result.split + ".json");
    TextResult path = canonical_path(file);
    if (path.success == false) {
        result.error = "Spider questions not found";
        return result;
    }
    if (path_inside(path.text, result.root) == false) {
        result.error = "Spider question file escapes dataset directory";
        return result;
    }
    TextResult text = read_text(path.text, 64 * 1024 * 1024);
    if (text.success == false) {
        result.error = text.error;
        return result;
    }
    if (json_has_nul_escape(text.text)) {
        result.error = "Dataset JSON contains NUL";
        return result;
    }
    cJSON *array = json_parse(text.text);
    if (json_is_array(array) == false) {
        free_json(array);
        result.error = "Spider split must be a valid JSON array";
        return result;
    }
    int count = json_size(array);
    if (count > options->limit) {
        count = options->limit;
    }
    if (count < 1) {
        free_json(array);
        result.error = "Empty Spider split";
        return result;
    }
    result.items = new (nothrow) DatasetItem[count];
    if (result.items == nullptr) {
        free_json(array);
        result.error = "Cannot allocate dataset";
        return result;
    }
    result.count = count;
    cJSON *item = json_first_item(array);
    for (int i = 0; i < count; i++) {
        TextResult id = read_json_string(item, "db_id");
        TextResult question = read_json_string(item, "question");
        TextResult query = read_json_string(item, "query");
        bool valid = true;
        if (json_is_object(item) == false) {
            valid = false;
        }
        if (id.success == false) {
            valid = false;
        }
        if (question.success == false) {
            valid = false;
        }
        if (query.success == false) {
            valid = false;
        }
        if (valid == false) {
            result.error = "Spider item missing db_id, question or query at index " + to_string(i);
            break;
        }
        DatasetItem *destination = &result.items[i];
        destination->db_id = id.text;
        destination->question = question.text;
        destination->query = query.text;
        item = json_next_item(item);
    }
    free_json(array);
    if (result.error.empty() == false) {
        delete[] result.items;
        result.items = nullptr;
        result.count = 0;
        return result;
    }
    result.success = true;
    return result;
}
