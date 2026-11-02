// 字段读取只返回成功/失败，配置错误在 load_config 出口统一处理。
bool config_number(cJSON *object, const char *name, int *destination, int minimum, int maximum) {
    if (json_has_field(object, name) == false) {
        return true;
    }
    NumberResult value = read_json_number(object, name);
    if (value.success == false) {
        return false;
    }
    if (floor(value.number) != value.number) {
        return false;
    }
    if (value.number < minimum || value.number > maximum) {
        return false;
    }
    *destination = (int)value.number;
    return true;
}
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
ConfigResult load_config(string path, bool required) {
    ConfigResult result;
    result.error = error_message("配置");
    Config *config = &result.config;
    ApiOptions *api = &config->api;
    DatabaseOptions *db = &config->database;
    bool valid = true;
    // 结构体已提供默认值，JSON 只覆盖实际填写的字段。
    if (file_exists(path)) {
        TextResult text = read_text(path, 65536);
        cJSON *object = json_parse(text.text);
        valid = text.success && json_is_object(object);
        cJSON *section = json_field(object, "api");
        if (section != nullptr) {
            if (json_is_object(section) == false) {
                valid = false;
            }
            if (config_string(section, "base_url", &api->base_url) == false) {
                valid = false;
            }
            if (config_string(section, "model", &api->model) == false) {
                valid = false;
            }
            if (config_string(section, "api_key", &api->api_key) == false) {
                valid = false;
            }
            if (config_number(section, "timeout_seconds", &api->timeout_seconds, 1, 600) == false) {
                valid = false;
            }
            if (config_number(section, "max_tokens", &api->max_tokens, 1, 32768) == false) {
                valid = false;
            }
        }
        section = json_field(object, "database");
        if (section != nullptr) {
            if (json_is_object(section) == false) {
                valid = false;
            }
            if (config_string(section, "path", &config->database_path) == false) {
                valid = false;
            }
            if (config_number(section, "timeout_ms", &db->timeout_ms, 1, 600000) == false) {
                valid = false;
            }
            if (config_number(section, "max_display_rows", &db->max_display_rows, 1, 10000) ==
                false) {
                valid = false;
            }
            if (config_number(section, "max_preview_rows", &db->max_preview_rows, 1, 100) ==
                false) {
                valid = false;
            }
            if (config_number(section, "max_sql_bytes", &db->max_sql_bytes, 1, 65536) == false) {
                valid = false;
            }
            if (config_number(section, "max_result_bytes", &db->max_result_bytes, 1024, 16777216) ==
                false) {
                valid = false;
            }
        }
        free_json(object);
    } else if (required) {
        valid = false;
    }
    if (valid == false) {
        return result;
    }
    if (config->database_path.empty() == false) {
        // 相对数据库路径以配置文件所在目录为基准。
        config->database_path = relative_path(path, config->database_path);
    }
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
