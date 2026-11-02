// JSON 库调用集中在这里。其他模块使用简单的读取函数和结果结构体。
bool is_utf8_tail(unsigned char value) {
    if (value < 128) {
        return false;
    }
    if (value > 191) {
        return false;
    }
    return true;
}
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
cJSON *json_first_item(cJSON *array) {
    if (array == nullptr) {
        return nullptr;
    }
    return array->child;
}
cJSON *json_next_item(cJSON *item) {
    if (item == nullptr) {
        return nullptr;
    }
    return item->next;
}
cJSON *json_item(cJSON *array, int index) {
    return cJSON_GetArrayItem(array, index);
}
int json_size(cJSON *array) {
    return cJSON_GetArraySize(array);
}
cJSON *json_array() {
    return cJSON_CreateArray();
}
TextResult read_json_string(cJSON *object, const char *name) {
    cJSON *value = json_field(object, name);
    if (cJSON_IsString(value) == false) {
        return failed_text(string("Expected JSON string: ") + name);
    }
    TextResult result;
    result.success = true;
    result.text = value->valuestring;
    return result;
}
NumberResult read_json_number(cJSON *object, const char *name) {
    NumberResult result;
    cJSON *value = json_field(object, name);
    if (cJSON_IsNumber(value) == false) {
        result.error = string("Expected JSON number: ") + name;
        return result;
    }
    if (isfinite(value->valuedouble) == false) {
        result.error = string("Invalid JSON number: ") + name;
        return result;
    }
    result.success = true;
    result.number = value->valuedouble;
    return result;
}
cJSON *json_string(string value) {
    return cJSON_CreateRaw(json_quote(value).c_str());
}
bool json_set(cJSON *object, const char *key, cJSON *value) {
    if (object == nullptr) {
        free_json(value);
        return false;
    }
    if (value == nullptr) {
        return false;
    }
    if (cJSON_AddItemToObject(object, key, value) == false) {
        free_json(value);
        return false;
    }
    return true;
}
bool json_add(cJSON *array, cJSON *value) {
    if (array == nullptr) {
        free_json(value);
        return false;
    }
    if (value == nullptr) {
        return false;
    }
    if (cJSON_AddItemToArray(array, value) == false) {
        free_json(value);
        return false;
    }
    return true;
}
TextResult json_text(cJSON *value) {
    if (value == nullptr) {
        return failed_text("Cannot allocate JSON output");
    }
    char *text = cJSON_PrintUnformatted(value);
    free_json(value);
    if (text == nullptr) {
        return failed_text("Cannot serialize JSON output");
    }
    TextResult result;
    result.success = true;
    result.text = text;
    cJSON_free(text);
    return result;
}
Status append_json_message(cJSON *messages, string role, string text) {
    cJSON *message = cJSON_CreateObject();
    bool valid = true;
    if (json_set(message, "role", json_string(role)) == false) {
        valid = false;
    }
    if (json_set(message, "content", json_string(text)) == false) {
        valid = false;
    }
    if (valid == false) {
        free_json(message);
        return failed_status("无法创建模型消息");
    }
    if (json_add(messages, message) == false) {
        return failed_status("无法创建模型上下文");
    }
    return ok_status();
}
TextResult model_request_json(Completion *source) {
    ApiOptions *api = &source->api;
    cJSON *body = cJSON_CreateObject();
    bool valid = true;
    if (json_set(body, "model", json_string(api->model)) == false) {
        valid = false;
    }
    if (json_set(body, "messages", cJSON_Duplicate(source->messages, true)) == false) {
        valid = false;
    }
    if (json_set(body, "stream", cJSON_CreateBool(false)) == false) {
        valid = false;
    }
    if (json_set(body, "temperature", cJSON_CreateNumber(0)) == false) {
        valid = false;
    }
    if (json_set(body, "max_tokens", cJSON_CreateNumber(api->max_tokens)) == false) {
        valid = false;
    }
    cJSON *thinking = cJSON_CreateObject();
    if (json_set(thinking, "type", json_string("disabled")) == false) {
        valid = false;
    }
    if (json_set(body, "thinking", thinking) == false) {
        valid = false;
    }
    if (valid == false) {
        free_json(body);
        return failed_text("无法创建 API 请求");
    }
    return json_text(body);
}
TextResult read_model_response(string text) {
    cJSON *object = json_parse(text);
    cJSON *choices = json_field(object, "choices");
    cJSON *choice = json_item(choices, 0);
    cJSON *message = json_field(choice, "message");
    TextResult result;
    TextResult role = read_json_string(message, "role");
    TextResult content = read_json_string(message, "content");
    if (json_is_array(choices) == false) {
        result.error = "模型未返回文本";
    } else if (role.success == false) {
        result.error = "模型未返回文本";
    } else if (role.text != "assistant") {
        result.error = "模型未返回文本";
    } else if (content.success == false) {
        result.error = "模型未返回文本";
    }
    if (result.error.empty()) {
        if (json_has_field(choice, "finish_reason")) {
            TextResult finish = read_json_string(choice, "finish_reason");
            if (finish.success == false) {
                result.error = "模型回复不完整";
            } else if (finish.text != "stop") {
                result.error = "模型回复不完整";
            }
        }
    }
    if (result.error.empty()) {
        result.text = trim(content.text);
        result.success = true;
    }
    free_json(object);
    return result;
}
string cell_text(Cell *cell) {
    if (cell->type == SQLITE_NULL) {
        return "NULL";
    }
    if (cell->type == SQLITE_INTEGER) {
        return to_string(cell->integer);
    }
    if (cell->type == SQLITE_TEXT) {
        return cell->text;
    }
    if (cell->type == SQLITE_BLOB) {
        return "0x" + cell->text;
    }
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.17g", cell->real);
    return buffer;
}
int cell_bytes(Cell *cell) {
    if (cell->type == SQLITE_TEXT) {
        return (int)json_quote(cell->text).length();
    }
    if (cell->type == SQLITE_BLOB) {
        return (int)cell->text.length() + 15;
    }
    return (int)cell_text(cell).length();
}
