#pragma once
#include "config.h"
#include <functional>

namespace t2s {
using Completion = std::function<Json(const Json& messages,const Json& tools)>;
Json parse_completion(const Json& response);
Json parse_completion_text(const std::string& response);
Json request_body(const ApiOptions& options,const Json& messages,const Json& tools);
Json curl_post(const ApiOptions& options,const Json& body);
Completion deepseek(ApiOptions options);
Completion mock_demo(bool repair=false);
Json tool_call(const std::string& id,const std::string& name,const Json& args);
}
