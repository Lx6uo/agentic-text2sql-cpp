#include "llm.h"
#include <algorithm>
#include <cstring>
#include <curl/curl.h>
#include <set>
#include <stdexcept>

namespace t2s {
namespace {
constexpr size_t response_limit=2*1024*1024;
struct CurlGlobal {
    CurlGlobal() {
        if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)
            throw std::runtime_error("Cannot initialize libcurl");
    }
    ~CurlGlobal(){curl_global_cleanup();}
};
struct CurlHandle {
    CURL* value=curl_easy_init();
    CurlHandle(){if(!value)throw std::runtime_error("Cannot create libcurl handle");}
    ~CurlHandle(){curl_easy_cleanup(value);}
};
struct CurlHeaders {
    curl_slist* value=nullptr;
    ~CurlHeaders(){curl_slist_free_all(value);}
    void append(const std::string& header) {
        auto* next=curl_slist_append(value,header.c_str());
        if(!next)throw std::runtime_error("Cannot allocate HTTP headers");
        value=next;
    }
};
struct ResponseBuffer {
    std::string data;
    bool too_large=false;
};
size_t receive(char* data,size_t size,size_t count,void* context) noexcept {
    auto& response=*static_cast<ResponseBuffer*>(context);
    if(size!=0&&count>static_cast<size_t>(-1)/size){response.too_large=true;return 0;}
    size_t bytes=size*count;
    if(bytes>response_limit-response.data.size()){response.too_large=true;return 0;}
    try{response.data.append(data,bytes);}catch(...){return 0;}
    return bytes;
}
int progress(void*,curl_off_t,curl_off_t,curl_off_t,curl_off_t) noexcept {
    return cancelled?1:0;
}
template<typename T>
void option(CURL* handle,CURLoption key,T value) {
    if(curl_easy_setopt(handle,key,value)!=CURLE_OK)
        throw std::runtime_error("Cannot configure libcurl request");
};
void validate(const ApiOptions& api) {
    // HTTPS only; redirects and curl configuration discovery are disabled.
    if(!api.base_url.starts_with("https://") || api.base_url.size()>2048 ||
       api.base_url.find_first_of("\r\n\t \"\\?#")!=std::string::npos || api.base_url.find('\0')!=std::string::npos)
        throw std::runtime_error("API base_url must be a plain HTTPS URL");
    auto host=api.base_url.substr(8,api.base_url.find('/',8)-8);
    if(host.empty()||host.find('@')!=std::string::npos)throw std::runtime_error("Invalid API host");
    if(api.api_key.empty())throw std::runtime_error("Missing API key: set DEEPSEEK_API_KEY or config.json");
    if(api.api_key.size()>4096 || std::any_of(api.api_key.begin(),api.api_key.end(),[](unsigned char c){return c<33||c==127;}))
        throw std::runtime_error("Invalid API key format");
    if(api.timeout_seconds<1||api.timeout_seconds>600)throw std::runtime_error("Invalid API timeout");
}
}
Json request_body(const ApiOptions& api,const Json& messages,const Json& tools) {
    Json body{{"model",api.model},{"messages",messages},{"stream",false},{"thinking",{{"type","disabled"}}},{"max_tokens",api.max_tokens}};
    if(!tools.empty()){body["tools"]=tools;body["tool_choice"]="auto";}
    return body;
}
Json parse_completion(const Json& r) {
    try {
        if(r.contains("error"))throw std::runtime_error("API returned an error");
        if(!r.at("choices").is_array()||r.at("choices").empty())throw std::runtime_error("Missing API choices");
        const auto& choice=r.at("choices")[0];
        auto finish=choice.value("finish_reason",std::string("stop"));
        if(finish!="stop"&&finish!="tool_calls")throw std::runtime_error("Incomplete API response (token limit or filtering)");
        const auto& m=choice.at("message");
        if(m.at("role")!="assistant")throw std::runtime_error("Expected assistant response");
        Json out{{"role","assistant"},{"content",nullptr}};
        if(m.contains("content")&&!m["content"].is_null()) {
            if(!m["content"].is_string())throw std::runtime_error("Invalid assistant content");
            out["content"]=m["content"];
        }
        if(m.contains("tool_calls")&&!m["tool_calls"].is_null()) {
            const auto& calls=m["tool_calls"];
            if(!calls.is_array()||calls.size()>16)throw std::runtime_error("Invalid tool_calls");
            std::set<std::string> ids;
            for(const auto& t:calls) {
                auto id=t.at("id").get<std::string>();
                if(id.empty()||!ids.insert(id).second||t.at("type")!="function"||
                   !t.at("function").at("name").is_string()||!t.at("function").at("arguments").is_string())
                    throw std::runtime_error("Invalid tool call envelope");
            }
            if(!calls.empty())out["tool_calls"]=calls;
        }
        if(!out.contains("tool_calls")&&(out["content"].is_null()||out["content"].get<std::string>().empty()))
            throw std::runtime_error("Empty assistant response");
        return Json{{"message",out},{"usage",r.value("usage",Json::object())}};
    }catch(const nlohmann::json::exception&){throw std::runtime_error("Malformed API response structure");}
}
Json parse_completion_text(const std::string& text) {
    Json r;try{r=Json::parse(text);}catch(...){throw std::runtime_error("Invalid API JSON response");}
    return parse_completion(r);
}
Json curl_post(const ApiOptions& api,const Json& body) {
    validate(api);if(cancelled)throw std::runtime_error("API request cancelled");
    static CurlGlobal global;
    CurlHandle handle;CurlHeaders headers;ResponseBuffer response;
    auto request=body.dump(-1,' ',false,Json::error_handler_t::replace);
    std::string authorization="Authorization: Bearer "+api.api_key;
    headers.append(authorization);headers.append("Content-Type: application/json");headers.append("Accept: application/json");
    // Best-effort overwrite of the caller-owned header copy. libcurl keeps its own
    // curl_slist copy only for the lifetime of this request.
    std::fill(authorization.begin(),authorization.end(),'\0');
    std::string url=api.base_url;while(url.back()=='/')url.pop_back();url+="/chat/completions";
    char error[CURL_ERROR_SIZE]={};
    option(handle.value,CURLOPT_URL,url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    option(handle.value,CURLOPT_PROTOCOLS_STR,"https");
#else
    option(handle.value,CURLOPT_PROTOCOLS,static_cast<long>(CURLPROTO_HTTPS));
#endif
    option(handle.value,CURLOPT_FOLLOWLOCATION,0L);
    option(handle.value,CURLOPT_POST,1L);
    option(handle.value,CURLOPT_POSTFIELDS,request.data());
    option(handle.value,CURLOPT_POSTFIELDSIZE_LARGE,static_cast<curl_off_t>(request.size()));
    option(handle.value,CURLOPT_HTTPHEADER,headers.value);
    option(handle.value,CURLOPT_WRITEFUNCTION,receive);
    option(handle.value,CURLOPT_WRITEDATA,&response);
    option(handle.value,CURLOPT_XFERINFOFUNCTION,progress);
    option(handle.value,CURLOPT_XFERINFODATA,static_cast<void*>(nullptr));
    option(handle.value,CURLOPT_NOPROGRESS,0L);
    option(handle.value,CURLOPT_NOSIGNAL,1L);
    option(handle.value,CURLOPT_CONNECTTIMEOUT_MS,static_cast<long>(std::min(10,api.timeout_seconds)*1000));
    option(handle.value,CURLOPT_TIMEOUT_MS,static_cast<long>(api.timeout_seconds*1000));
    option(handle.value,CURLOPT_MAXFILESIZE_LARGE,static_cast<curl_off_t>(response_limit));
    option(handle.value,CURLOPT_SSL_VERIFYPEER,1L);
    option(handle.value,CURLOPT_SSL_VERIFYHOST,2L);
    option(handle.value,CURLOPT_NETRC,static_cast<long>(CURL_NETRC_IGNORED));
    option(handle.value,CURLOPT_USERAGENT,"agentic-text2sql-cpp/1.0");
    option(handle.value,CURLOPT_ACCEPT_ENCODING,"");
    option(handle.value,CURLOPT_ERRORBUFFER,error);
    auto status=curl_easy_perform(handle.value);
    if(response.too_large)throw std::runtime_error("API response exceeds 2 MB limit");
    if(status==CURLE_ABORTED_BY_CALLBACK&&cancelled)throw std::runtime_error("API request cancelled");
    if(status==CURLE_OPERATION_TIMEDOUT)throw std::runtime_error("API request timed out");
    if(status!=CURLE_OK)throw std::runtime_error(std::string("libcurl transport failed: ")+curl_easy_strerror(status));
    long http_status=0;
    if(curl_easy_getinfo(handle.value,CURLINFO_RESPONSE_CODE,&http_status)!=CURLE_OK)
        throw std::runtime_error("Cannot read API HTTP status");
    if(http_status<200||http_status>=300)throw std::runtime_error("API HTTP error: "+std::to_string(http_status));
    Json parsed;try{parsed=Json::parse(response.data);}catch(...){throw std::runtime_error("Invalid API JSON response");}
    return parsed;
}
Completion deepseek(ApiOptions api) {
    return [api=std::move(api)](const Json& messages,const Json& tools){return parse_completion(curl_post(api,request_body(api,messages,tools)));};
}
Json tool_call(const std::string& id,const std::string& name,const Json& args) {
    return Json{{"id",id},{"type","function"},{"function",{{"name",name},{"arguments",args.dump()}}}};
}
Completion mock_demo(bool repair) {
    return [repair,step=0](const Json&,const Json&)mutable {
        Json message{{"role","assistant"},{"content",nullptr}};
        if(step==0)message["tool_calls"]=Json::array({tool_call("tables","list_tables",Json::object())});
        else if(step==1)message["tool_calls"]=Json::array({tool_call("schema","describe_tables",{{"tables",{"employees","departments"}}})});
        else if(step==2&&repair)message["tool_calls"]=Json::array({tool_call("bad","execute_sql",{{"sql","SELECT department_name, AVG(salary) FROM employees GROUP BY department_name"}})});
        else if(step==2|| (step==3&&repair))message["tool_calls"]=Json::array({tool_call("query","execute_sql",{{"sql","SELECT d.name AS department, ROUND(AVG(e.salary), 2) AS average_salary FROM employees e JOIN departments d ON e.department_id = d.id GROUP BY d.id, d.name ORDER BY average_salary DESC LIMIT 3"}})});
        else message["content"]="已按平均工资降序查询三个部门。结果来自真实 SQLite 执行。";
        ++step;return Json{{"message",message},{"usage",Json::object()}};
    };
}
}
