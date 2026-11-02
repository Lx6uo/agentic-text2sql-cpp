// API 不做逐项预校验，统一判断请求是否成功并统一释放资源。
const int response_limit = 2 * 1024 * 1024;
struct ResponseBuffer {
    char *data = nullptr;
    size_t used = 0;
};
size_t receive_response(char *data, size_t size, size_t count, void *context) {
    // libcurl 分块回传响应，累积到固定缓冲区，超过上限就结束接收。
    ResponseBuffer *response = (ResponseBuffer *)context;
    if (size != 0 && count > (size_t)-1 / size) {
        return 0;
    }
    size_t bytes = size * count;
    if (bytes > response_limit - response->used) {
        return 0;
    }
    memcpy(response->data + response->used, data, bytes);
    response->used += bytes;
    response->data[response->used] = '\0';
    return bytes;
}
int http_progress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return cancelled != 0;
}
TextResult curl_post(ApiOptions *api, string request) {
    TextResult result;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return failed_text("API");
    }
    CURL *handle = curl_easy_init();
    curl_slist *headers = nullptr;
    ResponseBuffer response;
    response.data = new (nothrow) char[response_limit + 1];
    bool ready = handle != nullptr && response.data != nullptr;
    if (ready) {
        response.data[0] = '\0';
        string authorization = "Authorization: Bearer " + api->api_key;
        const char *values[] = {authorization.c_str(), "Content-Type: application/json",
                                "Accept: application/json"};
        for (int i = 0; i < 3; i++) {
            curl_slist *next = curl_slist_append(headers, values[i]);
            if (next == nullptr) {
                ready = false;
                break;
            }
            headers = next;
        }
        for (int i = 0; i < (int)authorization.length(); i++) {
            authorization[i] = '\0';
        }
    }
    if (ready) {
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
        options[count++] = curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        options[count++] =
            curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, (long)api->timeout_seconds * 1000);
        options[count++] = curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
        options[count++] = curl_easy_setopt(handle, CURLOPT_USERAGENT, "text2sql-cpp/1.0");
        options[count++] = curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
        for (int i = 0; i < count; i++) {
            if (options[i] != CURLE_OK) {
                ready = false;
            }
        }
        if (ready) {
            CURLcode code = curl_easy_perform(handle);
            long status = 0;
            CURLcode info = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
            bool success = code == CURLE_OK && info == CURLE_OK;
            if (status < 200 || status >= 300) {
                success = false;
            }
            if (success) {
                result.text.assign(response.data, response.used);
                result.success = true;
            }
        }
    }
    // 请求成功或失败都经过这里，统一释放缓冲区、请求头和连接。
    delete[] response.data;
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    curl_global_cleanup();
    if (result.success == false) {
        result.error = error_message("API");
    }
    return result;
}
void close_completion(Completion *source) {
    free_json(source->messages);
    source->messages = nullptr;
}
Status start_completion(Completion *source, string system, string input) {
    close_completion(source);
    source->messages = json_array();
    Status result = append_json_message(source->messages, "system", system);
    if (result.success) {
        result = append_json_message(source->messages, "user", input);
    }
    if (result.success == false) {
        close_completion(source);
        result.error = error_message("API");
    }
    return result;
}
TextResult request_text(Completion *source) {
    // 把上下文转成 JSON，发送 API 请求，再取出模型回复文字。
    TextResult result;
    if (cancelled) {
        return failed_text("API");
    }
    TextResult request = model_request_json(source);
    if (request.success && request.text.length() <= 524288) {
        TextResult response = curl_post(&source->api, request.text);
        if (response.success) {
            result = read_model_response(response.text);
        }
    }
    if (result.text.empty()) {
        result.success = false;
    }
    if (result.success == false) {
        result.error = error_message("API");
    }
    return result;
}
