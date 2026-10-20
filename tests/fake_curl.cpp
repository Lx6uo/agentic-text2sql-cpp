// Test-only curl substitute. Never linked into the product.
#include "common.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>
using namespace t2s;
int main(int argc,char** argv) {
    try {
        std::string auth,body;
        for(int i=1;i+1<argc;++i) {
            std::string a=argv[i];
            if(a.find("fake-secret")!=std::string::npos)return 91;
            if(a=="--header")auth=std::string(argv[++i]).substr(1);
            else if(a=="--data-binary")body=std::string(argv[++i]).substr(1);
        }
        struct stat st{};
        if(auth.empty()||stat(auth.c_str(),&st)!=0||(st.st_mode&0777)!=0600||std::getenv("DEEPSEEK_API_KEY"))return 92;
        auto dir=auth.substr(0,auth.rfind('/'));
        if(stat(dir.c_str(),&st)!=0||(st.st_mode&0777)!=0700)return 93;
        if(const char* capture=std::getenv("T2S_CAPTURE_FILE")) {
            std::ofstream saved(capture);saved<<Json{{"temporary_directory",dir},{"pid",getpid()}};
        }
        std::ifstream auth_in(auth);std::string content((std::istreambuf_iterator<char>(auth_in)),{});
        if(content.find("Authorization: Bearer fake-secret")==std::string::npos)return 94;
        std::ifstream in(body);Json request;in>>request;
        if(request["stream"]!=false||request["thinking"]["type"]!="disabled"||request["model"]!="deepseek-flash")return 95;
        std::string mode=std::getenv("T2S_FAKE_MODE")?std::getenv("T2S_FAKE_MODE"):"agent";
        if(mode=="http_error"){std::cout<<"{\"error\":\"fake-secret\"}\n401";return 0;}
        if(mode=="bad_json"){std::cout<<"{broken\n200";return 0;}
        if(mode=="bad_envelope"){std::cout<<"{}\n200";return 0;}
        if(mode=="transport"){return 7;}
        if(mode=="timeout"){return 28;}
        if(mode=="huge"){std::cout<<std::string(3*1024*1024,'x');return 0;}
        if(mode=="sleep"){usleep(10000000);return 0;}
        Json message{{"role","assistant"},{"content",nullptr}};
        auto& messages=request["messages"];
        bool has_tools=request.contains("tools");
        if(!has_tools) {
            auto data=Json::parse(messages[1]["content"].get<std::string>());
            if(data.contains("reference_sql")||data.contains("query"))return 96;
            message["content"]="SELECT COUNT(*) FROM employees";
        }else {
            int calls=0;std::string last;
            for(const auto& m:messages)if(m["role"]=="tool") {++calls;last=m["content"].get<std::string>();}
            std::string name;Json args=Json::object();
            if(calls==0)name="list_tables";
            else if(calls==1){name="describe_tables";args={{"tables",{"employees"}}};}
            else if(calls==2){name="execute_sql";args={{"sql","SELECT missing FROM employees"}};}
            else if(calls==3){if(last.find("no such column")==std::string::npos)return 97;name="execute_sql";args={{"sql","SELECT COUNT(*) FROM employees"}};}
            else {if(last.find("success") == std::string::npos)return 98;message["content"]="已查询员工数量。";}
            if(!name.empty())message["tool_calls"]=Json::array({Json{{"id","call-"+std::to_string(calls)},{"type","function"},
                {"function",{{"name",name},{"arguments",args.dump()}}}}});
        }
        std::cout<<Json{{"choices",{Json{{"finish_reason",has_tools&&message.contains("tool_calls")?"tool_calls":"stop"},{"message",message}}}},
            {"usage",{{"prompt_tokens",10},{"completion_tokens",5},{"total_tokens",15}}}}.dump()<<"\n200";
        return 0;
    }catch(...){return 99;}
}
