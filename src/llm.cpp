#include "llm.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <set>
#include <spawn.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;

namespace t2s {
namespace {
constexpr size_t response_limit=2*1024*1024;
struct Temp {
    std::string directory;
    Temp() {
        char pattern[]="/tmp/text2sql-http-XXXXXX";
        char* p=mkdtemp(pattern);if(!p) throw std::runtime_error("Cannot create HTTP temporary directory");
        directory=p;
    }
    ~Temp() { unlink((directory+"/body").c_str());unlink((directory+"/auth").c_str());rmdir(directory.c_str()); }
    std::string write(const char* name,const std::string& text) {
        auto path=directory+"/"+name;
        int fd=open(path.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);
        if(fd<0) throw std::runtime_error("Cannot create HTTP temporary file");
        size_t offset=0;
        while(offset<text.size()) {
            auto n=::write(fd,text.data()+offset,text.size()-offset);
            if(n<0&&errno==EINTR)continue;
            if(n<=0){close(fd);throw std::runtime_error("Cannot write HTTP temporary file");}
            offset+=static_cast<size_t>(n);
        }
        close(fd);return path;
    }
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
struct Child {
    pid_t pid=-1; int fd=-1; bool reaped=false;
    ~Child(){if(fd>=0)close(fd);if(pid>0&&!reaped){kill(pid,SIGKILL);while(waitpid(pid,nullptr,0)<0&&errno==EINTR){}}}
};
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
    Temp temp;
    auto request=temp.write("body",body.dump(-1,' ',false,Json::error_handler_t::replace));
    auto auth=temp.write("auth","Authorization: Bearer "+api.api_key+"\nContent-Type: application/json\n");
    std::string url=api.base_url;while(url.back()=='/')url.pop_back();url+="/chat/completions";
    std::vector<std::string> args={"curl","-q","--globoff","--silent","--show-error","--proto","=https","--request","POST",
        "--max-time",std::to_string(api.timeout_seconds),"--connect-timeout",std::to_string(std::min(10,api.timeout_seconds)),
        "--max-filesize",std::to_string(response_limit),"--header","@"+auth,"--data-binary","@"+request,
        "--write-out","\n%{http_code}","--url",url};
    std::vector<char*> argv;for(auto& a:args)argv.push_back(a.data());argv.push_back(nullptr);
    // Do not inherit the API key environment into curl.
    std::vector<std::string> env;
    for(char** e=environ;*e;++e)if(!std::string(*e).starts_with("DEEPSEEK_API_KEY="))env.emplace_back(*e);
    std::vector<char*> envp;for(auto& e:env)envp.push_back(e.data());envp.push_back(nullptr);
    int pipes[2];if(pipe(pipes)!=0)throw std::runtime_error("Cannot create HTTP pipe");
    fcntl(pipes[0],F_SETFD,FD_CLOEXEC);fcntl(pipes[1],F_SETFD,FD_CLOEXEC);
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions,pipes[1],STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions,pipes[0]);posix_spawn_file_actions_addclose(&actions,pipes[1]);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,"/dev/null",O_WRONLY,0);
    Child child;child.fd=pipes[0];
    int status=posix_spawnp(&child.pid,"curl",&actions,nullptr,argv.data(),envp.data());
    posix_spawn_file_actions_destroy(&actions);close(pipes[1]);
    if(status!=0){child.pid=-1;throw std::runtime_error("Cannot start curl; install system curl for live API mode");}
    fcntl(child.fd,F_SETFL,O_NONBLOCK);
    auto start=Clock::now();std::string output;bool eof=false;int exit_status=0;
    while(!eof||!child.reaped) {
        if(cancelled)throw std::runtime_error("API request cancelled");
        if(elapsed_ms(start)>(api.timeout_seconds+2)*1000.0)throw std::runtime_error("API request timed out");
        pollfd p{eof?-1:child.fd,POLLIN,0};poll(&p,1,50);
        char buffer[8192];
        while(true) {
            auto n=read(child.fd,buffer,sizeof(buffer));
            if(n==0){eof=true;break;}
            if(n<0){if(errno==EINTR)continue;if(errno!=EAGAIN&&errno!=EWOULDBLOCK)throw std::runtime_error("HTTP response read failed");break;}
            output.append(buffer,static_cast<size_t>(n));
            if(output.size()>response_limit+16)throw std::runtime_error("API response exceeds 2 MB limit");
        }
        if(!child.reaped) {
            auto waited=waitpid(child.pid,&exit_status,WNOHANG);
            if(waited==child.pid)child.reaped=true;
            else if(waited<0&&errno!=EINTR)throw std::runtime_error("Cannot wait for curl");
        }
    }
    if(!WIFEXITED(exit_status)||WEXITSTATUS(exit_status)!=0) {
        if(WIFEXITED(exit_status)&&WEXITSTATUS(exit_status)==28)throw std::runtime_error("API request timed out");
        throw std::runtime_error("curl transport failed (exit "+std::to_string(WIFEXITED(exit_status)?WEXITSTATUS(exit_status):-1)+")");
    }
    auto newline=output.rfind('\n');
    if(newline==std::string::npos)throw std::runtime_error("Missing HTTP status");
    auto code=output.substr(newline+1);
    if(code.size()!=3||code[0]!='2')throw std::runtime_error("API HTTP error: "+code);
    Json response;try{response=Json::parse(output.substr(0,newline));}catch(...){throw std::runtime_error("Invalid API JSON response");}
    return response;
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
        if(step==0)message["tool_calls"]={tool_call("tables","list_tables",Json::object())};
        else if(step==1)message["tool_calls"]={tool_call("schema","describe_tables",{{"tables",{"employees","departments"}}})};
        else if(step==2&&repair)message["tool_calls"]={tool_call("bad","execute_sql",{{"sql","SELECT department_name, AVG(salary) FROM employees GROUP BY department_name"}})};
        else if(step==2|| (step==3&&repair))message["tool_calls"]={tool_call("query","execute_sql",{{"sql","SELECT d.name AS department, ROUND(AVG(e.salary), 2) AS average_salary FROM employees e JOIN departments d ON e.department_id = d.id GROUP BY d.id, d.name ORDER BY average_salary DESC LIMIT 3"}})};
        else message["content"]="已按平均工资降序查询三个部门。结果来自真实 SQLite 执行。";
        ++step;return Json{{"message",message},{"usage",Json::object()}};
    };
}
}
