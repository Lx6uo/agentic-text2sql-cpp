#include "benchmark.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sqlite3.h>
#include <stdexcept>

using namespace t2s;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
struct Workspace {
    fs::path root;
    Workspace() {
        auto stamp=std::chrono::high_resolution_clock::now().time_since_epoch().count();
        root=fs::temp_directory_path()/path_from_utf8("text2sql-跨平台-"+std::to_string(stamp));
        if(!fs::create_directory(root))throw std::runtime_error("Cannot create test workspace");
    }
    ~Workspace(){std::error_code error;fs::remove_all(root,error);}
};
void create_database(const fs::path& path) {
    sqlite3* raw=nullptr;
    if(sqlite3_open(path_to_utf8(path).c_str(),&raw)!=SQLITE_OK) {
        std::string error=raw?sqlite3_errmsg(raw):"allocation failed";sqlite3_close(raw);
        throw std::runtime_error("Cannot create fixture: "+error);
    }
    const char* sql="CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT);"
                    "INSERT INTO employees VALUES(1,'Alice'),(2,'李明');";
    char* error=nullptr;
    if(sqlite3_exec(raw,sql,nullptr,nullptr,&error)!=SQLITE_OK) {
        std::string message=error?error:"fixture SQL failed";sqlite3_free(error);sqlite3_close(raw);
        throw std::runtime_error(message);
    }
    sqlite3_close(raw);
}
std::string bytes(const fs::path& path) {
    std::ifstream input(path,std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)),{});
}
Completion completion() {
    return [](const Json& messages,const Json& tools) {
        Json message{{"role","assistant"},{"content",nullptr}};
        if(tools.empty())message["content"]="SELECT COUNT(*) FROM employees";
        else {
            size_t calls=0;for(const auto& item:messages)if(item["role"]=="tool")++calls;
            if(calls==0)message["tool_calls"]={tool_call("tables","list_tables",Json::object())};
            else if(calls==1)message["tool_calls"]={tool_call("schema","describe_tables",{{"tables",{"employees"}}})};
            else if(calls==2)message["tool_calls"]={tool_call("query","execute_sql",{{"sql","SELECT COUNT(*) FROM employees"}})};
            else message["content"]="There are two employees.";
        }
        return Json{{"message",message},{"usage",{{"total_tokens",5}}}};
    };
}
}
int main() {
    try {
        Workspace workspace;auto dataset=workspace.root/path_from_utf8("数据集");
        auto directory=dataset/"database"/"fixture";fs::create_directories(directory);
        auto database=directory/"fixture.sqlite";create_database(database);auto before=bytes(database);
        Json cases=Json::array();
        for(int i=0;i<3;++i)cases.push_back({{"db_id","fixture"},{"question","How many employees?"},{"query","SELECT COUNT(*) FROM employees"}});
        {std::ofstream output(dataset/"dev.json",std::ios::binary);output<<cases;check(static_cast<bool>(output),"write dev.json");}
        auto report=workspace.root/path_from_utf8("结果.jsonl");
        Config config;BenchmarkOptions options{path_to_utf8(dataset),"dev","both",path_to_utf8(report),path_to_utf8(workspace.root/"predictions"),3,false};
        auto result=run_benchmark(options,config,completion());
        check(result["success"],"benchmark success");
        for(auto mode:{"baseline","agentic"}) {
            check(result["modes"][mode]["cases"]==3,"case count");
            check(result["modes"][mode]["execution_successes"]==3,"execution success");
            check(result["modes"][mode]["result_matches"]==3,"result match");
            check(fs::exists(path_from_utf8(result["predictions"][mode].get<std::string>())),"prediction file");
        }
        check(bytes(database)==before,"database remains unchanged");
        int lines=0;std::string line;std::ifstream records(report);
        while(std::getline(records,line)){check(Json::parse(line).is_object(),"JSONL object");++lines;}
        check(lines==8,"metadata, cases and summary");
        bool preserved=false;try{run_benchmark(options,config,completion());}catch(...){preserved=true;}
        check(preserved,"existing output preserved");
        std::cout<<"Passed "<<checks<<" portable benchmark checks\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
