// HTTPS 请求和 SQL 回复。
const int response_limit = 2 * 1024 * 1024;
struct ResponseBuffer {
    char *data = nullptr;
    size_t used = 0;
    bool too_large = false;
};
size_t receive_response(char *data, size_t size, size_t count, void *context) {
    ResponseBuffer *response = (ResponseBuffer *)context;
    if (size != 0) {
        if (count > (size_t)-1 / size) {
            response->too_large = true;
            return 0;
        }
    }
    size_t bytes = size * count;
    if (bytes > response_limit - response->used) {
        response->too_large = true;
        return 0;
    }
    memcpy(response->data + response->used, data, bytes);
    response->used += bytes;
    response->data[response->used] = '\0';
    return bytes;
}
int http_progress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    if (cancelled) {
        return 1;
    }
    return 0;
}
Status validate_api(ApiOptions *api) {
    bool invalid_url = false;
    if (api->base_url.compare(0, 8, "https://") != 0) {
        invalid_url = true;
    }
    if (api->base_url.length() > 2048) {
        invalid_url = true;
    }
    if (api->base_url.find_first_of("\r\n\t \"\\?#") != string::npos) {
        invalid_url = true;
    }
    if (api->base_url.find('\0') != string::npos) {
        invalid_url = true;
    }
    if (invalid_url) {
        return failed_status("API base_url must be a plain HTTPS URL");
    }
    string host = api->base_url.substr(8, api->base_url.find('/', 8) - 8);
    if (host.empty()) {
        return failed_status("Invalid API host");
    }
    if (host.find('@') != string::npos) {
        return failed_status("Invalid API host");
    }
    if (api->api_key.empty()) {
        return failed_status("Missing API key: set DEEPSEEK_API_KEY or config.json");
    }
    if (api->api_key.length() > 4096) {
        return failed_status("Invalid API key format");
    }
    for (int i = 0; i < (int)api->api_key.length(); i++) {
        unsigned char c = api->api_key[i];
        if (c < 33) {
            return failed_status("Invalid API key format");
        }
        if (c == 127) {
            return failed_status("Invalid API key format");
        }
    }
    if (api->timeout_seconds < 1) {
        return failed_status("Invalid API timeout");
    }
    if (api->timeout_seconds > 600) {
        return failed_status("Invalid API timeout");
    }
    return ok_status();
}
TextResult curl_post(ApiOptions *api, string request) {
    TextResult result;
    Status valid = validate_api(api);
    if (!valid.success) {
        return failed_text(valid.error);
    }
    if (cancelled) {
        return failed_text("API request cancelled");
    }
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return failed_text("Cannot initialize libcurl");
    }
    CURL *handle = curl_easy_init();
    curl_slist *headers = nullptr;
    ResponseBuffer response;
    response.data = new (nothrow) char[response_limit + 1];
    if (handle == nullptr) {
        result.error = "Cannot allocate HTTP request";
    }
    if (response.data == nullptr) {
        result.error = "Cannot allocate HTTP request";
    }
    if (result.error.empty()) {
        response.data[0] = '\0';
        string authorization = "Authorization: Bearer " + api->api_key;
        headers = curl_slist_append(nullptr, authorization.c_str());
        for (int i = 0; i < (int)authorization.length(); i++) {
            authorization[i] = '\0';
        }
        if (headers == nullptr) {
            result.error = "Cannot allocate HTTP headers";
        }
        const char *extra[] = {"Content-Type: application/json", "Accept: application/json"};
        for (int i = 0; i < 2; i++) {
            if (result.error.empty() == false) {
                break;
            }
            curl_slist *added = curl_slist_append(headers, extra[i]);
            if (added == nullptr) {
                result.error = "Cannot allocate HTTP headers";
            } else {
                headers = added;
            }
        }
    }
    if (result.error.empty()) {
        string url = api->base_url;
        while (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        url += "/chat/completions";
        CURLcode options[32];
        int count = 0;
        options[count++] = curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
        options[count++] = curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
#else
        options[count++] = curl_easy_setopt(handle, CURLOPT_PROTOCOLS, (long)CURLPROTO_HTTPS);
#endif
        options[count++] = curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_POST, 1L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.data());
        options[count++] =
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request.length());
        options[count++] = curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        options[count++] = curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receive_response);
        options[count++] = curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        options[count++] = curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, http_progress);
        options[count++] = curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        int connect_seconds = api->timeout_seconds;
        if (connect_seconds > 10) {
            connect_seconds = 10;
        }
        options[count++] =
            curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, (long)connect_seconds * 1000);
        options[count++] =
            curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, (long)api->timeout_seconds * 1000);
        options[count++] =
            curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE, (curl_off_t)response_limit);
        options[count++] = curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
        options[count++] = curl_easy_setopt(handle, CURLOPT_USERAGENT, "agentic-text2sql-cpp/1.0");
        options[count++] = curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
        for (int i = 0; i < count; i++) {
            if (options[i] != CURLE_OK) {
                result.error = "Cannot configure libcurl request";
            }
        }
        if (result.error.empty()) {
            CURLcode code = curl_easy_perform(handle);
            if (response.too_large) {
                result.error = "API response exceeds 2 MB limit";
            } else if (code == CURLE_ABORTED_BY_CALLBACK) {
                if (cancelled) {
                    result.error = "API request cancelled";
                } else {
                    result.error = string("libcurl transport failed: ") + curl_easy_strerror(code);
                }
            } else if (code == CURLE_OPERATION_TIMEDOUT) {
                result.error = "API request timed out";
            } else if (code != CURLE_OK) {
                result.error = string("libcurl transport failed: ") + curl_easy_strerror(code);
            }
            if (result.error.empty()) {
                long status = 0;
                if (curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK) {
                    result.error = "Cannot read API HTTP status";
                } else if (status < 200 || status >= 300) {
                    result.error = "API HTTP error: " + to_string(status);
                } else {
                    result.text.assign(response.data, response.used);
                    result.success = true;
                }
            }
        }
    }
    delete[] response.data;
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    curl_global_cleanup();
    return result;
}
void close_completion(Completion *source) {
    free_json(source->messages);
    source->messages = nullptr;
}
Status add_message(Completion *source, string role, string text) {
    return append_json_message(source->messages, role, text);
}
Status start_completion(Completion *source, string system, string input) {
    close_completion(source);
    source->messages = json_array();
    Status result = add_message(source, "system", system);
    if (result.success) {
        result = add_message(source, "user", input);
    }
    if (result.success == false) {
        close_completion(source);
    }
    return result;
}
TextResult request_sql(Completion *source) {
    if (cancelled) {
        return failed_text("查询已取消");
    }
    TextResult request = sql_request_json(source);
    if (request.success == false) {
        return request;
    }
    if (request.text.length() > 524288) {
        return failed_text("模型上下文超过大小上限");
    }
    TextResult response = curl_post(&source->api, request.text);
    if (response.success == false) {
        return response;
    }
    if (json_has_nul_escape(response.text)) {
        return failed_text("模型回复包含 NUL");
    }
    TextResult result = read_sql_response(response.text);
    if (result.success == false) {
        return result;
    }
    if (result.text.compare(0, 3, "```") == 0) {
        size_t first = result.text.find('\n');
        size_t last = result.text.rfind("```");
        if (first == string::npos) {
            return failed_text("SQL 代码块不完整");
        }
        if (last <= first) {
            return failed_text("SQL 代码块不完整");
        }
        result.text = trim(result.text.substr(first + 1, last - first - 1));
    }
    if (result.text.empty()) {
        return failed_text("模型返回了空 SQL");
    }
    Status added = add_message(source, "assistant", result.text);
    if (added.success == false) {
        return failed_text(added.error);
    }
    return result;
}
