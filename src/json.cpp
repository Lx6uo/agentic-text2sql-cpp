// JSON 只用于配置、数据集和 AI 协议，不生成运行报告。
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
            bool valid = length > 0 && i + length <= (int)text.length();
            for (int j = 1; j < length && valid; j++) {
                unsigned char next = text[i + j];
                if (next < 128 || next > 191) valid = false;
                value = value * 64 + next - 128;
            }
            if (value < minimum || value > 1114111 || (value >= 55296 && value <= 57343))
                valid = false;
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
cJSON *json_string(string value) {
    return cJSON_CreateRaw(json_quote(value).c_str());
}
bool json_set(cJSON *object, const char *key, cJSON *value) {
    if (object == nullptr || value == nullptr) {
        cJSON_Delete(value);
        return false;
    }
    if (!cJSON_AddItemToObject(object, key, value)) {
        cJSON_Delete(value);
        return false;
    }
    return true;
}
bool json_add(cJSON *array, cJSON *value) {
    if (array == nullptr || value == nullptr) {
        cJSON_Delete(value);
        return false;
    }
    if (!cJSON_AddItemToArray(array, value)) {
        cJSON_Delete(value);
        return false;
    }
    return true;
}
TextResult json_text(cJSON *value) {
    if (value == nullptr) return failed_text("Cannot allocate JSON output");
    char *text = cJSON_PrintUnformatted(value);
    cJSON_Delete(value);
    if (text == nullptr) return failed_text("Cannot serialize JSON output");
    TextResult result;
    result.success = true;
    result.text = text;
    cJSON_free(text);
    return result;
}
bool json_has_nul_escape(string text) {
    if (text.find('\0') != string::npos) return true;
    for (int i = 0; i + 1 < (int)text.length(); i++) {
        if (text[i] != '\\') continue;
        if (text.compare(i, 6, "\\u0000") == 0) return true;
        i++;
    }
    return false;
}

string cell_text(Cell *cell) {
    if (cell->type == SQLITE_NULL) return "NULL";
    if (cell->type == SQLITE_INTEGER) return to_string(cell->integer);
    if (cell->type == SQLITE_TEXT) return cell->text;
    if (cell->type == SQLITE_BLOB) return "0x" + cell->text;
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.17g", cell->real);
    return buffer;
}
int cell_bytes(Cell *cell) {
    if (cell->type == SQLITE_TEXT) return (int)json_quote(cell->text).length();
    if (cell->type == SQLITE_BLOB) return (int)cell->text.length() + 15;
    return (int)cell_text(cell).length();
}
