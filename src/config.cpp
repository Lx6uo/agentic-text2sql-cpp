// 字段读取只返回成功/失败，配置错误在 load_config 出口统一处理。
// 读取可选文本，缺省时保留原值，类型错误时返回失败。
bool config_string(cJSON *object, const char *name, string *destination) {
    if (json_has_field(object, name) == false) {
        return true;
    }
    TextResult value = read_json_string(object, name);
    if (value.success == false) {
        return false;
    }
    *destination = value.text;
    return true;
}
// 合并默认值、JSON 配置和环境变量，返回完整运行配置。
ConfigResult load_config(string path, bool required) {
    ConfigResult result;
    result.error = error_message("配置");
    Config *config = &result.config;
    ApiOptions *api = &config->api;
    bool valid = true;
    // 结构体已提供默认值，JSON 只覆盖实际填写的字段。
    if (file_exists(path)) {
        TextResult text = read_text(path, 65536);
        cJSON *object = json_parse(text.text);
        valid = text.success && json_is_object(object);
        // 字段名与目标字符串按位置对应，缺省字段保留默认值。
        const char *sections[] = {"api", "api", "api", "database"};
        const char *names[] = {"base_url", "model", "api_key", "path"};
        string *destinations[] = {&api->base_url, &api->model, &api->api_key,
                                  &config->database_path};
        for (int i = 0; i < 4; i++) {
            cJSON *section = json_field(object, sections[i]);
            if (section != nullptr &&
                (!json_is_object(section) ||
                 !config_string(section, names[i], destinations[i]))) {
                valid = false;
            }
        }
        free_json(object);
    } else if (required) {
        // 显式指定 --config 时，配置文件必须存在。
        valid = false;
    }
    if (valid == false) {
        return result;
    }
    if (config->database_path.empty() == false) {
        // 相对数据库路径以配置文件所在目录为基准。
        config->database_path = relative_path(path, config->database_path);
    }
    // 环境变量优先于文件配置，空值也会覆盖原密钥。
#ifdef _WIN32
    char *key = nullptr;
    size_t length = 0;
    if (_dupenv_s(&key, &length, "DEEPSEEK_API_KEY") == 0 && key != nullptr) {
        api->api_key = key;
        free(key);
    }
#else
    const char *key = getenv("DEEPSEEK_API_KEY");
    if (key != nullptr) {
        api->api_key = key;
    }
#endif
    result.success = true;
    result.error.clear();
    return result;
}
