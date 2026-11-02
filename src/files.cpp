// 文件与路径接口集中在这里，返回 Status 或 TextResult。
namespace fs = std::filesystem;

bool file_exists(string path) {
    error_code error;
    return fs::exists(fs::u8path(path), error);
}
TextResult canonical_path(string path) {
    error_code error;
    fs::path resolved = fs::canonical(fs::u8path(path), error);
    if (error) return failed_text("Cannot resolve path: " + path);
    TextResult result;
    result.success = true;
    result.text = resolved.u8string();
    return result;
}
bool path_inside(string path, string root) {
    fs::path relative = fs::u8path(path).lexically_relative(fs::u8path(root));
    if (relative.empty() || relative.is_absolute()) return false;
    string text = relative.generic_u8string();
    if (text == ".." || text.compare(0, 3, "../") == 0) return false;
    return true;
}
TextResult read_text(string path, int max_bytes) {
    error_code error;
    fs::path native = fs::u8path(path);
    uintmax_t bytes = fs::file_size(native, error);
    if (error) return failed_text("Cannot read file: " + path);
    if (bytes > (uintmax_t)max_bytes) return failed_text("File exceeds size limit: " + path);
    ifstream input(native, ios::binary);
    if (!input) return failed_text("Cannot read file: " + path);
    TextResult result;
    char buffer[4096];
    while (input) {
        input.read(buffer, sizeof(buffer));
        int count = (int)input.gcount();
        if ((int)result.text.length() + count > max_bytes)
            return failed_text("File exceeds size limit: " + path);
        result.text.append(buffer, count);
    }
    if (!input.eof()) return failed_text("File read failed: " + path);
    result.success = true;
    return result;
}
Status open_output(OutputFile *output, string path) {
    error_code error;
    fs::path native = fs::u8path(path);
    fs::path parent = native.parent_path();
    if (!parent.empty()) {
        fs::create_directories(parent, error);
        if (error) return failed_status("Cannot create output directory");
    }
    bool exists = fs::exists(native, error);
    if (error) return failed_status("Cannot inspect output path");
    if (exists)
        return failed_status("Cannot create output (existing files are preserved): " + path);
    output->stream.open(native, ios::binary);
    if (!output->stream) return failed_status("Cannot create output: " + path);
    return ok_status();
}
Status write_output(OutputFile *output, string text) {
    output->stream.write(text.data(), (streamsize)text.length());
    output->stream.flush();
    if (!output->stream) return failed_status("Benchmark output write failed");
    return ok_status();
}
TextResult dataset_database(string root, string id) {
    if (id.empty() || id.length() > 128 || id == "." || id == "..")
        return failed_text("Unsafe Spider db_id");
    for (int i = 0; i < (int)id.length(); i++) {
        unsigned char c = id[i];
        if (!isalnum(c) && c != '_' && c != '-') return failed_text("Unsafe Spider db_id");
    }
    fs::path path = fs::u8path(root) / "database" / id / (id + ".sqlite");
    TextResult resolved = canonical_path(path.u8string());
    if (!resolved.success) return failed_text("Spider database not found: " + id);
    if (!path_inside(resolved.text, root))
        return failed_text("Spider database escapes dataset directory");
    return resolved;
}
DatasetResult load_dataset(BenchmarkOptions *options) {
    DatasetResult result;
    result.split = options->split;
    if (result.split == "train") result.split = "train_spider";
    if (result.split != "dev" && result.split != "train_spider" &&
        result.split != "train_others" && result.split != "test") {
        result.error = "Unsupported split: use dev, train_spider, train_others or test";
        return result;
    }
    TextResult root = canonical_path(options->dataset);
    if (!root.success) {
        result.error = root.error;
        return result;
    }
    result.root = root.text;
    fs::path file = fs::u8path(result.root) / (result.split + ".json");
    TextResult path = canonical_path(file.u8string());
    if (!path.success) {
        result.error = "Spider questions not found";
        return result;
    }
    if (!path_inside(path.text, result.root)) {
        result.error = "Spider question file escapes dataset directory";
        return result;
    }
    TextResult text = read_text(path.text, 64 * 1024 * 1024);
    if (!text.success) {
        result.error = text.error;
        return result;
    }
    if (json_has_nul_escape(text.text)) {
        result.error = "Dataset JSON contains NUL";
        return result;
    }
    cJSON *array =
        cJSON_ParseWithLengthOpts(text.text.c_str(), text.text.length() + 1, nullptr, true);
    if (!cJSON_IsArray(array)) {
        cJSON_Delete(array);
        result.error = "Spider split must be a valid JSON array";
        return result;
    }
    int count = cJSON_GetArraySize(array);
    if (count > options->limit) count = options->limit;
    if (count < 1) {
        cJSON_Delete(array);
        result.error = "Empty Spider split";
        return result;
    }
    result.items = new (nothrow) DatasetItem[count];
    if (result.items == nullptr) {
        cJSON_Delete(array);
        result.error = "Cannot allocate dataset";
        return result;
    }
    result.count = count;
    cJSON *item = array->child;
    for (int i = 0; i < count; i++) {
        cJSON *id = cJSON_GetObjectItemCaseSensitive(item, "db_id");
        cJSON *question = cJSON_GetObjectItemCaseSensitive(item, "question");
        cJSON *query = cJSON_GetObjectItemCaseSensitive(item, "query");
        if (!cJSON_IsObject(item) || !cJSON_IsString(id) || !cJSON_IsString(question) ||
            !cJSON_IsString(query)) {
            result.error = "Spider item missing db_id, question or query at index " + to_string(i);
            break;
        }
        result.items[i].db_id = id->valuestring;
        result.items[i].question = question->valuestring;
        result.items[i].query = query->valuestring;
        item = item->next;
    }
    cJSON_Delete(array);
    if (!result.error.empty()) {
        delete[] result.items;
        result.items = nullptr;
        result.count = 0;
        return result;
    }
    result.success = true;
    return result;
}
