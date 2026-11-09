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
