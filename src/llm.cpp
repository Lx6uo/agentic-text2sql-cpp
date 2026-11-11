// API 不做逐项预校验，统一判断请求是否成功并统一释放资源。
const int response_limit = 2 * 1024 * 1024;
// 累积响应分块，超过缓冲区上限时通知 libcurl 中止接收。
size_t receive_response(char *data, size_t size, size_t count, void *context) {
    string *response = (string *)context;
    if (size != 0 && count > (size_t)-1 / size) {
        return 0;
    }
    size_t bytes = size * count;
    if (bytes > response_limit - response->length()) {
        return 0;
    }
    response->append(data, bytes);
    return bytes;
}
int http_progress(void *, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    // 返回非零值通知 libcurl 取消请求。
    return cancelled != 0;
}
// 发送 HTTPS JSON 请求，返回响应正文或统一 API 错误。
TextResult curl_post(ApiOptions *api, string request) {
    TextResult result;
    // 先准备连接和请求头，响应由 string 接收。
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return failed_text("API");
    }
    CURL *handle = curl_easy_init();
    curl_slist *headers = nullptr;
    string response;
    bool ready = handle != nullptr;
    if (ready) {
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
        // 链表已复制请求头，清空临时密钥副本。
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
        // 禁止重定向，避免向其他地址转发密钥。
        options[count++] = curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 0L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_POST, 1L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request.data());
        options[count++] =
            curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)request.length());
        options[count++] = curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
        // 接收和进度回调分别负责容量限制与请求取消。
        options[count++] = curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, receive_response);
        options[count++] = curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
        options[count++] = curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, http_progress);
        options[count++] = curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 10000L);
        options[count++] =
            curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, (long)api_timeout_seconds * 1000);
        options[count++] = curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
        options[count++] = curl_easy_setopt(handle, CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
        options[count++] = curl_easy_setopt(handle, CURLOPT_USERAGENT, "text2sql-cpp/1.0");
        options[count++] = curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
        // 所有选项设置成功后才发请求，再检查 HTTP 状态码。
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
                result.text = response;
                result.success = true;
            }
        }
    }
    // 请求成功或失败都经过这里，统一释放请求头和连接。
    curl_slist_free_all(headers);
    curl_easy_cleanup(handle);
    curl_global_cleanup();
    if (result.success == false) {
        result.error = error_message("API");
    }
    return result;
}
// 序列化上下文、发送请求，再提取非空 assistant 回复。
TextResult ask_model(ApiOptions *api, string system, string input) {
    if (cancelled) {
        return failed_text("API");
    }
    string request = model_request_json(api, system, input);
    if (request.length() > request_limit) {
        return failed_text("API");
    }
    TextResult response = curl_post(api, request);
    if (!response.success) {
        return response;
    }
    TextResult result = read_model_response(response.text);
    if (result.success && result.text.empty()) {
        return failed_text("API");
    }
    return result;
}
