#include "config.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
namespace t2s {
namespace {
void number(const Json& j,const char* name,int& dest,int min,int max) {
    if(!j.contains(name)) return;
    if(!j[name].is_number_integer()) throw std::runtime_error(std::string("Config: expected integer for ")+name);
    auto value=j[name].get<int64_t>();
    if(value<min || value>max) throw std::runtime_error(std::string("Config: out of range: ")+name);
    dest=static_cast<int>(value);
}
void string_value(const Json& j,const char* name,std::string& dest) {
    if(!j.contains(name)) return;
    if(!j[name].is_string()) throw std::runtime_error(std::string("Config: expected string for ")+name);
    dest=j[name].get<std::string>();
}
}
Config load_config(const std::string& path,bool required) {
    Config c;
    auto native_path=path_from_utf8(path);
    if(std::filesystem::exists(native_path)) {
        if(std::filesystem::file_size(native_path)>65536) throw std::runtime_error("Config file exceeds 64 KB");
        std::ifstream in(native_path);
        if(!in) throw std::runtime_error("Cannot read config file");
        Json j; try{in>>j;}catch(...){throw std::runtime_error("Invalid config JSON");}
        if(!j.is_object()) throw std::runtime_error("Config must be a JSON object");
        for(auto key:{"api","agent","database"}) if(j.contains(key)&&!j[key].is_object()) throw std::runtime_error("Config sections must be objects");
        if(j.contains("api")) {
            auto& a=j["api"];string_value(a,"base_url",c.api.base_url);string_value(a,"model",c.api.model);string_value(a,"api_key",c.api.api_key);
            number(a,"timeout_seconds",c.api.timeout_seconds,1,600);number(a,"max_tokens",c.api.max_tokens,1,32768);
        }
        if(j.contains("agent")) {
            auto& a=j["agent"];number(a,"max_steps",c.agent.max_steps,1,100);number(a,"max_sql_attempts",c.agent.max_sql_attempts,1,100);number(a,"max_repairs",c.agent.max_repairs,0,99);
        }
        if(j.contains("database")) {
            auto& d=j["database"];number(d,"timeout_ms",c.database.timeout_ms,1,600000);
            number(d,"max_display_rows",c.database.max_display_rows,1,10000);number(d,"max_preview_rows",c.database.max_preview_rows,1,100);
            number(d,"max_sql_bytes",c.database.max_sql_bytes,1,65536);number(d,"max_result_bytes",c.database.max_result_bytes,1024,16777216);
        }
    } else if(required) throw std::runtime_error("Config file not found");
    if(const char* key=std::getenv("DEEPSEEK_API_KEY")) c.api.api_key=key;
    if(c.api.model.empty()) throw std::runtime_error("Model must not be empty");
    return c;
}
}
