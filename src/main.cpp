#include "agent.h"
#include "benchmark.h"
#include "demo.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>
#include <unistd.h>

using namespace t2s;
namespace {
const char* help=R"(Agentic Text-to-SQL — C++20 / SQLite / DeepSeek Flash
Usage:
  text2sql [--db PATH] [--config PATH] [--verbose] [--json]
  text2sql query --db PATH --question TEXT [--verbose] [--json]
  text2sql schema --db PATH [--json]
  text2sql demo [--repair] [--verbose] [--json]
  text2sql benchmark --dataset PATH [--split dev] [--limit 20]
                    [--mode agentic|baseline|both] [--output PATH]
                    [--predictions PATH] [--verbose] [--json]
  text2sql --help

Global option: --config PATH (default: config.json if present).
Live query/benchmark require DEEPSEEK_API_KEY or config.json and system curl.
Demo Mode (Mock LLM): no key, network or external dataset required.
Default startup enters a standard REPL. Commands:
  /help, /open <path>, /tables, /schema, /model, /verbose on|off, /exit
--json: stdout contains JSON only; progress and errors go to stderr.
Exit codes: 0 success, 1 query/runtime failure, 2 usage/config error, 130 cancelled.
)";
struct Args {
    std::string command="repl",db,question,config="config.json",dataset,split="dev",mode="agentic",output,predictions;
    int limit=20;bool verbose=false,json=false,repair=false,config_required=false,help=false;
};
Args parse(int argc,char** argv) {
    Args a;int i=1;
    if(i<argc && argv[i][0]!='-')a.command=argv[i++];
    if(a.command!="repl"&&a.command!="query"&&a.command!="schema"&&a.command!="demo"&&a.command!="benchmark")throw std::runtime_error("Unknown command: "+a.command);
    std::set<std::string> seen;
    for(;i<argc;++i) {
        std::string flag=argv[i];
        if(!seen.insert(flag).second)throw std::runtime_error("Repeated option: "+flag);
        if(flag=="--help"||flag=="-h"){a.help=true;continue;}
        if(flag=="--verbose"){a.verbose=true;continue;}
        if(flag=="--json"){a.json=true;continue;}
        if(flag=="--repair"){a.repair=true;continue;}
        auto value=[&](){if(i+1>=argc)throw std::runtime_error("Missing value for "+flag);return std::string(argv[++i]);};
        if(flag=="--db")a.db=value();else if(flag=="--question")a.question=value();
        else if(flag=="--config"){a.config=value();a.config_required=true;}
        else if(flag=="--dataset")a.dataset=value();else if(flag=="--split")a.split=value();
        else if(flag=="--mode")a.mode=value();else if(flag=="--output")a.output=value();
        else if(flag=="--predictions")a.predictions=value();
        else if(flag=="--limit") {
            auto s=value();size_t pos=0;
            try{a.limit=std::stoi(s,&pos);}catch(...){throw std::runtime_error("--limit requires a positive integer");}
            if(pos!=s.size()||a.limit<1||a.limit>100000)throw std::runtime_error("--limit must be between 1 and 100000");
        }else throw std::runtime_error("Unknown option: "+flag);
    }
    if(a.help)return a;
    if((a.command=="query"||a.command=="schema")&&a.db.empty())throw std::runtime_error("--db is required");
    if(a.command=="query"&&trim(a.question).empty())throw std::runtime_error("--question is required");
    if(a.command=="benchmark"&&a.dataset.empty())throw std::runtime_error("--dataset is required");
    auto relevant=[&](const char* flag,bool ok){if(seen.contains(flag)&&!ok)throw std::runtime_error(std::string(flag)+" is not valid for "+a.command);};
    relevant("--repair",a.command=="demo");relevant("--question",a.command=="query");
    relevant("--db",a.command=="repl"||a.command=="query"||a.command=="schema");
    for(auto f:{"--dataset","--split","--mode","--limit","--output","--predictions"})relevant(f,a.command=="benchmark");
    if(a.mode!="agentic"&&a.mode!="baseline"&&a.mode!="both")throw std::runtime_error("--mode requires agentic, baseline or both");
    return a;
}
std::string safe_text(std::string s) {
    std::string out;
    for(unsigned char c:s) {
        if(c=='\n'){out+="\\n";}else if(c=='\t'){out+="\\t";}else if(c<32||c==127){out+='?';}else out+=static_cast<char>(c);
    }
    return out;
}
size_t width(const std::string& s) {
    size_t n=0;for(unsigned char c:s)if((c&0xc0)!=0x80)n+=c>=0xe0?2:1;return n;
}
void table(const Json& q) {
    auto columns=q.at("columns");std::vector<std::vector<std::string>> rows;
    std::vector<size_t> widths;
    for(const auto& c:columns)widths.push_back(std::min<size_t>(40,width(c.get<std::string>())));
    for(const auto& r:q.at("rows")) {
        std::vector<std::string> row;
        for(size_t c=0;c<r.size();++c) {
            auto s=safe_text(r[c].is_null()?"NULL":r[c].is_string()?r[c].get<std::string>():r[c].dump());
            if(width(s)>40) {
                size_t end=0,w=0;
                while(end<s.size()&&w<37){unsigned char ch=s[end];size_t count=ch<128?1:ch<224?2:ch<240?3:4;w+=ch>=224?2:1;end+=count;}
                s=s.substr(0,end)+"...";
            }
            widths[c]=std::max(widths[c],width(s));row.push_back(std::move(s));
        }
        rows.push_back(std::move(row));
    }
    auto line=[&](const std::vector<std::string>& row) {
        for(size_t c=0;c<row.size();++c)std::cout<<row[c]<<std::string(widths[c]>width(row[c])?widths[c]-width(row[c])+2:2,' ');
        std::cout<<'\n';
    };
    std::vector<std::string> names;for(auto& c:columns)names.push_back(safe_text(c.get<std::string>()));line(names);
    for(auto& row:rows)line(row);
    std::cout<<rows.size()<<" row(s)"<<(q.value("truncated",false)?" (truncated)":"")<<'\n';
}
void print_result(const Json& r,bool json,bool verbose) {
    if(json){std::cout<<r.dump(-1,' ',false,Json::error_handler_t::replace)<<'\n';return;}
    if(r.contains("mode"))std::cout<<r["mode"].get<std::string>()<<'\n';
    if(r.contains("query")&&!r["query"].is_null()) {
        auto& q=r["query"];
        std::cout<<"\nSQL:\n"<<q["sql"].get<std::string>()<<'\n';
        if(q["success"].get<bool>()){std::cout<<"\nResults:\n";table(q);}
    }
    if(r.contains("answer")&&!r["answer"].get<std::string>().empty())std::cout<<'\n'<<safe_text(r["answer"].get<std::string>())<<'\n';
    if(r.contains("error")&&!r["error"].is_null())std::cerr<<"Error: "<<safe_text(r["error"].get<std::string>())<<'\n';
    if(verbose&&r.contains("agent_steps"))std::cout<<"Agent steps: "<<r["agent_steps"]<<", SQL attempts: "<<r["sql_attempts"]<<", repairs: "<<r["repairs"]<<", elapsed: "<<r["elapsed_ms"]<<" ms\n";
}
void show_schema(Database& db,bool json) {
    auto s=db.schema();if(json){std::cout<<Json{{"success",true},{"schema",s}}.dump()<<'\n';return;}
    for(const auto& t:s) {
        std::cout<<safe_text(t["name"].get<std::string>())<<" ("<<t["type"].get<std::string>()<<")\n";
        for(const auto& c:t["columns"])std::cout<<"  "<<safe_text(c["name"].get<std::string>())<<" "<<safe_text(c["type"].get<std::string>())<<(c["pk"].get<int>()?" PRIMARY KEY":"")<<'\n';
        for(const auto& f:t["foreign_keys"])std::cout<<"  FK "<<f["from"]<<" -> "<<f["table"]<<'.'<<f["to"]<<'\n';
    }
}
int repl(const Args& args,const Config& config) {
    std::unique_ptr<Database> db;bool verbose=args.verbose;
    if(!args.db.empty())db=std::make_unique<Database>(args.db,config.database);
    auto say=[&](const std::string& text){if(args.json)std::cout<<Json{{"success",true},{"message",text}}.dump()<<'\n';else std::cout<<text<<'\n';};
    if(!args.json)std::cout<<"Agentic Text-to-SQL\nModel: "<<safe_text(config.api.model)<<"\nType /help for commands.\n\n";
    std::string input;
    while(!cancelled) {
        if(!args.json&&isatty(STDIN_FILENO))std::cout<<"> "<<std::flush;
        if(!std::getline(std::cin,input))break;
        input=trim(input);if(input.empty())continue;
        try {
            if(input[0]=='/') {
                auto space=input.find_first_of(" \t");auto command=input.substr(0,space);
                auto param=space==std::string::npos?"":trim(input.substr(space));
                if(command=="/exit"&&param.empty()){say("Goodbye.");return 0;}
                if(command=="/help"&&param.empty()){say(help);continue;}
                if(command=="/model"&&param.empty()) {say("Model: "+config.api.model+"\nBase URL: "+config.api.base_url+"\nAPI key: "+(config.api.api_key.empty()?"not configured":"configured"));continue;}
                if(command=="/verbose") {
                    if(param!="on"&&param!="off")throw std::runtime_error("Usage: /verbose on|off");
                    verbose=param=="on";say("Verbose: "+param);continue;
                }
                if(command=="/open") {
                    if(param.size()>1&&((param.front()=='"'&&param.back()=='"')||(param.front()=='\''&&param.back()=='\'')))param=param.substr(1,param.size()-2);
                    auto opened=std::make_unique<Database>(param,config.database);auto count=opened->list_tables().size();db=std::move(opened);
                    say("Connected to "+param+"\nTables: "+std::to_string(count));continue;
                }
                if(command=="/schema"&&param.empty()){if(!db)throw std::runtime_error("Open a database with /open <path>");show_schema(*db,args.json);continue;}
                if(command=="/tables"&&param.empty()) {
                    if(!db)throw std::runtime_error("Open a database with /open <path>");auto tables=db->list_tables();
                    if(args.json)std::cout<<Json{{"success",true},{"tables",tables}}.dump()<<'\n';
                    else for(const auto& t:tables)std::cout<<safe_text(t["name"].get<std::string>())<<'\n';
                    continue;
                }
                throw std::runtime_error("Unknown command or arguments. Type /help.");
            }
            if(!db)throw std::runtime_error("Open a database with /open <path>");
            auto result=run_agent(*db,input,deepseek(config.api),config.agent,verbose);print_result(result,args.json,verbose);
        }catch(const std::exception& e){print_result(Json{{"success",false},{"error",e.what()}},args.json,verbose);}
    }
    return cancelled?130:0;
}
}
int main(int argc,char** argv) {
    struct sigaction action{};action.sa_handler=[](int){cancelled=1;};sigemptyset(&action.sa_mask);sigaction(SIGINT,&action,nullptr);
    bool json=false;for(int i=1;i<argc;++i)if(std::string(argv[i])=="--json")json=true;
    Args args;Config config;
    try {
        args=parse(argc,argv);
        if(args.help){if(args.json)std::cout<<Json{{"help",help}}.dump()<<'\n';else std::cout<<help;return 0;}
        config=load_config(args.config,args.config_required);
    }catch(const std::exception& e){print_result(Json{{"success",false},{"error",e.what()}},json,false);return 2;}
    try {
        if(args.command=="repl")return repl(args,config);
        if(args.command=="schema"){Database db(args.db,config.database);show_schema(db,args.json);return 0;}
        Json result;
        if(args.command=="demo") {
            // Generated at configure time directly from examples/demo.sql.
            auto db=Database::from_sql(demo_sql,config.database);
            result=run_agent(*db,"查询平均工资最高的三个部门",mock_demo(args.repair),config.agent,args.verbose);
            result["mode"]="Demo Mode (Mock LLM)";
        }else if(args.command=="query") {
            Database db(args.db,config.database);result=run_agent(db,args.question,deepseek(config.api),config.agent,args.verbose);
        }else {
            if(config.api.api_key.empty())throw std::runtime_error("Missing API key: set DEEPSEEK_API_KEY or config.json");
            BenchmarkOptions options{args.dataset,args.split,args.mode,args.output,args.predictions,args.limit,args.verbose};
            result=run_benchmark(options,config,deepseek(config.api));
            if(args.json)std::cout<<result.dump(-1,' ',false,Json::error_handler_t::replace)<<'\n';
            else std::cout<<result.dump(2,' ',false,Json::error_handler_t::replace)<<'\n';
            return cancelled?130:result.value("success",false)?0:1;
        }
        print_result(result,args.json,args.verbose);return cancelled?130:result.value("success",false)?0:1;
    }catch(const std::exception& e){print_result(Json{{"success",false},{"error",e.what()}},args.json,false);return cancelled?130:1;}
}
