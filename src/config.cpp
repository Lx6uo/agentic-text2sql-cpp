// 配置只在读取 JSON 时使用库指针，其余代码使用 Config 结构体。
bool config_number(cJSON *object, const char *name, int *destination, int minimum, int maximum,
                   string *error) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (value == nullptr) return true;
    if (!cJSON_IsNumber(value) || !isfinite(value->valuedouble) ||
        floor(value->valuedouble) != value->valuedouble) {
        *error = string("Config: expected integer for ") + name;
        return false;
    }
    if (value->valuedouble < minimum || value->valuedouble > maximum) {
        *error = string("Config: out of range: ") + name;
        return false;
    }
    *destination = (int)value->valuedouble;
    return true;
}
bool config_string(cJSON *object, const char *name, string *destination, string *error) {
    cJSON *value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (value == nullptr) return true;
    if (!cJSON_IsString(value)) {
        *error = string("Config: expected string for ") + name;
        return false;
    }
    *destination = value->valuestring;
    return true;
}
bool read_api_config(cJSON *object, ApiOptions *api, string *error) {
    if (!config_string(object, "base_url", &api->base_url, error)) return false;
    if (!config_string(object, "model", &api->model, error)) return false;
    if (!config_string(object, "api_key", &api->api_key, error)) return false;
    if (!config_number(object, "timeout_seconds", &api->timeout_seconds, 1, 600, error))
        return false;
    return config_number(object, "max_tokens", &api->max_tokens, 1, 32768, error);
}
bool read_agent_config(cJSON *object, AgentOptions *agent, string *error) {
    if (!config_number(object, "max_steps", &agent->max_steps, 1, 100, error)) return false;
    if (!config_number(object, "max_sql_attempts", &agent->max_sql_attempts, 1, 100, error))
        return false;
    return config_number(object, "max_repairs", &agent->max_repairs, 0, 99, error);
}
bool read_database_config(cJSON *object, DatabaseOptions *db, string *path, string *error) {
    if (!config_string(object, "path", path, error)) return false;
    if (!config_number(object, "timeout_ms", &db->timeout_ms, 1, 600000, error)) return false;
    if (!config_number(object, "max_display_rows", &db->max_display_rows, 1, 10000, error))
        return false;
    if (!config_number(object, "max_preview_rows", &db->max_preview_rows, 1, 100, error))
        return false;
    if (!config_number(object, "max_sql_bytes", &db->max_sql_bytes, 1, 65536, error)) return false;
    return config_number(object, "max_result_bytes", &db->max_result_bytes, 1024, 16777216, error);
}
ConfigResult load_config(string path, bool required) {
    ConfigResult result;
    if (file_exists(path)) {
        TextResult text = read_text(path, 65536);
        if (!text.success) {
            result.error = text.error;
            return result;
        }
        if (json_has_nul_escape(text.text)) {
            result.error = "Config JSON contains NUL";
            return result;
        }
        cJSON *object =
            cJSON_ParseWithLengthOpts(text.text.c_str(), text.text.length() + 1, nullptr, true);
        if (!cJSON_IsObject(object)) {
            cJSON_Delete(object);
            result.error = "Config must be a valid JSON object";
            return result;
        }
        const char *names[] = {"api", "agent", "database"};
        for (int i = 0; i < 3; i++) {
            cJSON *section = cJSON_GetObjectItemCaseSensitive(object, names[i]);
            if (section == nullptr) continue;
            if (!cJSON_IsObject(section)) {
                result.error = "Config sections must be objects";
                break;
            }
            bool valid = false;
            if (i == 0) valid = read_api_config(section, &result.config.api, &result.error);
            if (i == 1) valid = read_agent_config(section, &result.config.agent, &result.error);
            if (i == 2)
                valid = read_database_config(section, &result.config.database,
                                             &result.config.database_path, &result.error);
            if (!valid) break;
        }
        cJSON_Delete(object);
        if (!result.error.empty()) return result;
        if (!result.config.database_path.empty()) {
            fs::path database = fs::u8path(result.config.database_path);
            if (database.is_relative())
                result.config.database_path =
                    (fs::u8path(path).parent_path() / database).u8string();
        }
    } else if (required) {
        result.error = "Config file not found";
        return result;
    }
#ifdef _WIN32
    char *key = nullptr;
    size_t length = 0;
    if (_dupenv_s(&key, &length, "DEEPSEEK_API_KEY") == 0 && key != nullptr) {
        result.config.api.api_key = key;
        free(key);
    }
#else
    const char *key = getenv("DEEPSEEK_API_KEY");
    if (key != nullptr) result.config.api.api_key = key;
#endif
    if (result.config.api.model.empty()) {
        result.error = "Model must not be empty";
        return result;
    }
    result.success = true;
    return result;
}
