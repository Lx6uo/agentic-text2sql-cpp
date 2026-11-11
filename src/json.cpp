// JSON 库调用集中在这里。其他模块使用简单的读取函数和结果结构体。
bool is_utf8_tail(unsigned char value) {
    return value >= 128 && value <= 191;
}
// 转义引号和控制字符，替换非法 UTF-8，生成合法 JSON 字符串。
string json_quote(string text) {
    string result = "\"";
    const char *hex = "0123456789abcdef";
    for (int i = 0; i < (int)text.length(); i++) {
        unsigned char c = text[i];
        if (c == '"' || c == '\\') {
            result += '\\';
            result += c;
        } else if (c < 32) {
            result += "\\u00";
            result += hex[c / 16];
            result += hex[c % 16];
        } else if (c < 128) {
            result += c;
        } else {
            // 按首字节确定编码长度，再检查续字节和码点范围。
            int length = 0;
            int value = 0;
            int minimum = 0;
            if (c >= 194 && c <= 223) {
                length = 2;
                value = c - 192;
                minimum = 128;
            }
            if (c >= 224 && c <= 239) {
                length = 3;
                value = c - 224;
                minimum = 2048;
            }
            if (c >= 240 && c <= 244) {
                length = 4;
                value = c - 240;
                minimum = 65536;
            }
            bool valid = true;
            if (length == 0) {
                valid = false;
            }
            if (i + length > (int)text.length()) {
                valid = false;
            }
            for (int j = 1; j < length; j++) {
                if (valid == false) {
                    break;
                }
                unsigned char next = text[i + j];
                if (is_utf8_tail(next) == false) {
                    valid = false;
                }
                value = value * 64 + next - 128;
            }
            // 排除过长编码、代理区和 Unicode 范围外的码点。
            if (value < minimum) {
                valid = false;
            }
            if (value > 1114111) {
                valid = false;
            }
            bool surrogate = value >= 55296 && value <= 57343;
            if (surrogate) {
                valid = false;
            }
            if (valid) {
                result += text.substr(i, length);
                i += length - 1;
            } else {
                result += "\xef\xbf\xbd";
            }
        }
    }
    return result + '"';
}
bool json_has_nul_escape(string text) {
    // 拒绝 NUL，避免 cJSON 的字符串字段被提前截断。
    if (text.find('\0') != string::npos) {
        return true;
    }
    for (int i = 0; i + 1 < (int)text.length(); i++) {
        if (text[i] != '\\') {
            continue;
        }
        if (text.compare(i, 6, "\\u0000") == 0) {
            return true;
        }
        i++;
    }
    return false;
}
cJSON *json_parse(string text) {
    if (json_has_nul_escape(text)) {
        return nullptr;
    }
    // 要求完整 JSON，拒绝末尾追加的其他文本。
    return cJSON_ParseWithLengthOpts(text.c_str(), text.length() + 1, nullptr, true);
}
void free_json(cJSON *value) {
    cJSON_Delete(value);
}
cJSON *json_field(cJSON *object, const char *name) {
    return cJSON_GetObjectItemCaseSensitive(object, name);
}
bool json_has_field(cJSON *object, const char *name) {
    return json_field(object, name) != nullptr;
}
bool json_is_object(cJSON *value) {
    return cJSON_IsObject(value);
}
bool json_is_array(cJSON *value) {
    return cJSON_IsArray(value);
}
cJSON *json_item(cJSON *array, int index) {
    return cJSON_GetArrayItem(array, index);
}
// 字段必须存在且为字符串，否则返回失败状态。
TextResult read_json_string(cJSON *object, const char *name) {
    cJSON *value = json_field(object, name);
    if (cJSON_IsString(value) == false) {
        return failed_text("JSON");
    }
    TextResult result;
    result.success = true;
    result.text = value->valuestring;
    return result;
}
// 请求格式固定，只对模型名和两条消息的内容做 JSON 转义。
string model_request_json(ApiOptions *api, string system, string input) {
    return "{\"model\":" + json_quote(api->model) +
           ",\"messages\":[{\"role\":\"system\",\"content\":" + json_quote(system) +
           "},{\"role\":\"user\",\"content\":" + json_quote(input) +
           "}],\"stream\":false,\"temperature\":0,\"max_tokens\":" +
           to_string(model_output_tokens) + ",\"thinking\":{\"type\":\"disabled\"}}";
}
// 提取第一条 assistant 回复，校验角色、内容和结束状态。
TextResult read_model_response(string text) {
    cJSON *object = json_parse(text);
    cJSON *choices = json_field(object, "choices");
    cJSON *choice = json_item(choices, 0);
    cJSON *message = json_field(choice, "message");
    TextResult role = read_json_string(message, "role");
    TextResult content = read_json_string(message, "content");
    bool valid = json_is_array(choices) && role.success;
    if (role.text != "assistant") {
        valid = false;
    }
    if (content.success == false) {
        valid = false;
    }
    if (json_has_field(choice, "finish_reason")) {
        // 中断的回复可能只有半条 SQL，不能执行。
        TextResult finish = read_json_string(choice, "finish_reason");
        if (valid && finish.success && finish.text == "length") {
            free_json(object);
            TextResult limited;
            limited.error = "模型回复达到长度上限。";
            return limited;
        }
        if (finish.success == false || finish.text != "stop") {
            valid = false;
        }
    }
    free_json(object);
    if (valid == false) {
        return failed_text("API");
    }
    content.text = trim(content.text);
    return content;
}
// 只在读取结果时裁剪，标记也计入字节上限。
string limit_text(string text, int max_bytes, bool *truncated) {
    if ((int)text.length() <= max_bytes) {
        return text;
    }
    string marker = "[截断]";
    int end = max_bytes - (int)marker.length();
    while (end > 0 && is_utf8_tail((unsigned char)text[end])) {
        end--;
    }
    *truncated = true;
    return text.substr(0, end) + marker;
}
string columns_json(QueryResult *query) {
    string text = "[";
    for (int c = 0; c < query->column_count; c++) {
        if (c > 0) {
            text += ",";
        }
        text += json_quote(query->columns[c]);
    }
    return text + "]";
}
string row_json(Cell *cells, int column_count) {
    string text = "[";
    for (int c = 0; c < column_count; c++) {
        if (c > 0) {
            text += ",";
        }
        text += cells[c].is_null ? "null" : json_quote(cells[c].text);
    }
    return text + "]";
}
// 不再筛选或裁剪，完整打包终端显示的同一份结果。
string result_json(QueryResult *query) {
    string text = "{\"row_count\":" + to_string(query->row_count) +
                  ",\"complete\":" + (query->truncated ? "false" : "true") +
                  ",\"columns\":" + columns_json(query) + ",\"rows\":[";
    for (int r = 0; r < query->row_count; r++) {
        if (r > 0) {
            text += ",";
        }
        text += row_json(query->cells + r * query->column_count, query->column_count);
    }
    return text + "]}";
}
