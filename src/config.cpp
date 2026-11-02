// 配置通过简单的 JSON 读取接口转换成普通结构体。
bool config_number(cJSON *object, const char *name, int *destination, int minimum, int maximum,
                   string *error) {
    if (json_has_field(object, name) == false) {
        return true;
    }
    NumberResult value = read_json_number(object, name);
    if (value.success == false) {
        *error = string("Config: expected integer for ") + name;
        return false;
    }
    if (floor(value.number) != value.number) {
        *error = string("Config: expected integer for ") + name;
        return false;
    }
    bool outside_range = false;
    if (value.number < minimum) {
        outside_range = true;
    }
    if (value.number > maximum) {
        outside_range = true;
    }
    if (outside_range) {
        *error = string("Config: out of range: ") + name;
        return false;
    }
    *destination = (int)value.number;
    return true;
}
bool config_string(cJSON *object, const char *name, string *destination, string *error) {
    if (json_has_field(object, name) == false) {
        return true;
    }
    TextResult value = read_json_string(object, name);
    if (value.success == false) {
        *error = string("Config: expected string for ") + name;
        return false;
    }
    *destination = value.text;
    return true;
}
bool read_api_config(cJSON *object, ApiOptions *api, string *error) {
    if (config_string(object, "base_url", &api->base_url, error) == false) {
        return false;
    }
    if (config_string(object, "model", &api->model, error) == false) {
        return false;
    }
    if (config_string(object, "api_key", &api->api_key, error) == false) {
        return false;
    }
    if (config_number(object, "timeout_seconds", &api->timeout_seconds, 1, 600, error) == false) {
        return false;
    }
    return config_number(object, "max_tokens", &api->max_tokens, 1, 32768, error);
}
bool read_database_config(cJSON *object, DatabaseOptions *db, string *path, string *error) {
    if (config_string(object, "path", path, error) == false) {
        return false;
    }
    if (config_number(object, "timeout_ms", &db->timeout_ms, 1, 600000, error) == false) {
        return false;
    }
    if (config_number(object, "max_display_rows", &db->max_display_rows, 1, 10000, error) ==
        false) {
        return false;
    }
    if (config_number(object, "max_preview_rows", &db->max_preview_rows, 1, 100, error) == false) {
        return false;
    }
    if (config_number(object, "max_sql_bytes", &db->max_sql_bytes, 1, 65536, error) == false) {
        return false;
    }
    return config_number(object, "max_result_bytes", &db->max_result_bytes, 1024, 16777216, error);
}
ConfigResult load_config(string path, bool required) {
    ConfigResult result;
    Config *config = &result.config;
    if (file_exists(path)) {
        TextResult text = read_text(path, 65536);
        if (text.success == false) {
            result.error = text.error;
            return result;
        }
        if (json_has_nul_escape(text.text)) {
            result.error = "Config JSON contains NUL";
            return result;
        }
        cJSON *object = json_parse(text.text);
        if (json_is_object(object) == false) {
            free_json(object);
            result.error = "Config must be a valid JSON object";
            return result;
        }
        const char *names[] = {"api", "database"};
        for (int i = 0; i < 2; i++) {
            cJSON *section = json_field(object, names[i]);
            if (section == nullptr) {
                continue;
            }
            if (json_is_object(section) == false) {
                result.error = "Config sections must be objects";
                break;
            }
            bool valid = false;
            if (i == 0) {
                valid = read_api_config(section, &config->api, &result.error);
            }
            if (i == 1) {
                valid = read_database_config(section, &config->database, &config->database_path,
                                             &result.error);
            }
            if (valid == false) {
                break;
            }
        }
        free_json(object);
        if (result.error.empty() == false) {
            return result;
        }
        if (config->database_path.empty() == false) {
            config->database_path = relative_path(path, config->database_path);
        }
    } else if (required) {
        result.error = "Config file not found";
        return result;
    }
    ApiOptions *api = &config->api;
#ifdef _WIN32
    char *key = nullptr;
    size_t length = 0;
    if (_dupenv_s(&key, &length, "DEEPSEEK_API_KEY") == 0) {
        if (key != nullptr) {
            api->api_key = key;
            free(key);
        }
    }
#else
    const char *key = getenv("DEEPSEEK_API_KEY");
    if (key != nullptr) {
        api->api_key = key;
    }
#endif
    if (api->model.empty()) {
        result.error = "Model must not be empty";
        return result;
    }
    result.success = true;
    return result;
}
