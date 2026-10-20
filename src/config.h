#pragma once
#include "database.h"

namespace t2s {
struct ApiOptions {
    std::string base_url="https://api.deepseek.com";
    std::string model="deepseek-flash";
    std::string api_key;
    int timeout_seconds=45;
    int max_tokens=2048;
};
struct AgentOptions {
    int max_steps=8;
    int max_sql_attempts=3;
    int max_repairs=2;
};
struct Config {
    ApiOptions api;
    AgentOptions agent;
    DatabaseOptions database;
};
Config load_config(const std::string& path="config.json", bool required=false);
}
