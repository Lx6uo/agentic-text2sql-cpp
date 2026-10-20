#include "common.h"
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <fcntl.h>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sqlite3.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
using namespace t2s;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool condition,const std::string& message){++checks;if(!condition)throw std::runtime_error(message);}
struct Sandbox {
    fs::path dir;
    Sandbox(){char p[]="/tmp/text2sql-tests-XXXXXX";char* d=mkdtemp(p);if(!d)throw std::runtime_error("temp directory");dir=d;}
    ~Sandbox(){std::error_code ec;fs::remove_all(dir,ec);}
};
struct Result {int code;std::string out,err;};
Result run(const std::string& program,const std::vector<std::string>& args,const fs::path& dir,const std::string& input="",bool interrupt=false,bool live_stdin=false) {
    static int sequence=0;auto stem=dir/("process-"+std::to_string(sequence++));
    {std::ofstream in(stem.string()+".in");in<<input;}
    posix_spawn_file_actions_t actions;posix_spawn_file_actions_init(&actions);
    int stdin_pipe[2]={-1,-1};
    if(live_stdin) {
        if(pipe(stdin_pipe)!=0)throw std::runtime_error("stdin pipe");
        posix_spawn_file_actions_adddup2(&actions,stdin_pipe[0],STDIN_FILENO);
        posix_spawn_file_actions_addclose(&actions,stdin_pipe[0]);
        posix_spawn_file_actions_addclose(&actions,stdin_pipe[1]);
    }else posix_spawn_file_actions_addopen(&actions,STDIN_FILENO,(stem.string()+".in").c_str(),O_RDONLY,0);
    posix_spawn_file_actions_addopen(&actions,STDOUT_FILENO,(stem.string()+".out").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    posix_spawn_file_actions_addopen(&actions,STDERR_FILENO,(stem.string()+".err").c_str(),O_WRONLY|O_CREAT|O_TRUNC,0600);
    std::vector<std::string> storage{program};storage.insert(storage.end(),args.begin(),args.end());
    std::vector<char*> argv;for(auto& s:storage)argv.push_back(s.data());argv.push_back(nullptr);
    pid_t pid=-1;int error=posix_spawn(&pid,program.c_str(),&actions,nullptr,argv.data(),environ);
    posix_spawn_file_actions_destroy(&actions);if(error)throw std::runtime_error("spawn CLI failed");
    if(live_stdin)close(stdin_pipe[0]);
    int status=0;auto start=Clock::now();bool signalled=false;
    while(true) {
        auto waited=waitpid(pid,&status,WNOHANG);
        if(waited==pid)break;
        if(waited<0&&errno!=EINTR)throw std::runtime_error("wait CLI failed");
        if(interrupt&&!signalled&&elapsed_ms(start)>250){kill(pid,SIGINT);signalled=true;}
        if(elapsed_ms(start)>12000){kill(pid,SIGKILL);waitpid(pid,&status,0);throw std::runtime_error("CLI hung");}
        poll(nullptr,0,10);
    }
    if(live_stdin)close(stdin_pipe[1]);
    auto read=[](const std::string& p){std::ifstream f(p);return std::string((std::istreambuf_iterator<char>(f)),{});};
    return {WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status),read(stem.string()+".out"),read(stem.string()+".err")};
}
Json one_json(const Result& r) {
    try {return Json::parse(r.out);}catch(...){throw std::runtime_error("stdout is not one JSON document: "+r.out);}
}
void fixture_db(const fs::path& p) {
    sqlite3* db=nullptr;check(sqlite3_open(p.c_str(),&db)==SQLITE_OK,"create fixture DB");
    const char* sql="CREATE TABLE departments(id INTEGER PRIMARY KEY,name TEXT); CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,department_id INTEGER REFERENCES departments(id),salary REAL); INSERT INTO departments VALUES(1,'研发'); INSERT INTO employees VALUES(1,'Alice',1,100),(2,'李明',1,NULL);";
    check(sqlite3_exec(db,sql,nullptr,nullptr,nullptr)==SQLITE_OK,"seed fixture");sqlite3_close(db);
}
std::string bytes(const fs::path& p){std::ifstream in(p,std::ios::binary);return std::string((std::istreambuf_iterator<char>(in)),{});}
}
int main(int argc,char** argv) {
    try {
        check(argc==3,"expected product and fake curl paths");
        Sandbox box;auto executable=fs::absolute(argv[1]).string();auto fake=fs::absolute(argv[2]);
        fs::current_path(box.dir);auto bindir=box.dir/"bin";fs::create_directories(bindir);fs::copy_file(fake,bindir/"curl");
        auto path=bindir.string()+":"+(std::getenv("PATH")?std::getenv("PATH"):"");setenv("PATH",path.c_str(),1);
        unsetenv("DEEPSEEK_API_KEY");
        auto db=box.dir/"database with 空格.sqlite";fixture_db(db);auto before=bytes(db);
        auto r=run(executable,{"--help"},box.dir);check(r.code==0&&r.out.find("REPL")!=std::string::npos,"help");
        r=run(executable,{"--help","--json"},box.dir);check(one_json(r).contains("help"),"JSON help");
        for(auto flags:std::vector<std::vector<std::string>>{{"demo"},{"demo","--repair","--verbose"}}) {
            r=run(executable,flags,box.dir);check(r.code==0&&r.out.find("115000")!=std::string::npos&&r.out.find("Demo Mode (Mock LLM)")!=std::string::npos,"demo");
        }
        r=run(executable,{"demo","--repair","--json","--verbose"},box.dir);auto j=one_json(r);
        check(r.code==0&&j["sql_attempts"]==2&&j["repairs"]==1&&!r.err.empty(),"JSON verbose repair");
        r=run(executable,{"schema","--db",db.string(),"--json"},box.dir);check(r.code==0&&one_json(r)["schema"].size()==2,"schema no key");
        r=run(executable,{"schema","--db",db.string()},box.dir);check(r.code==0&&r.out.find("PRIMARY KEY")!=std::string::npos&&r.out.find("FK")!=std::string::npos,"schema text");
        r=run(executable,{"query","--db",db.string(),"--question","count","--json"},box.dir);check(r.code==1&&one_json(r)["error"].get<std::string>().find("Missing API key")!=std::string::npos,"missing key");
        r=run(executable,{"schema","--db","missing.sqlite","--json"},box.dir);check(r.code==1&&!one_json(r)["success"].get<bool>(),"missing DB");
        for(auto flags:std::vector<std::vector<std::string>>{{"query"},{"query","--db",db.string()},{"demo","--question","oops"},{"benchmark","--dataset","x","--limit","-1"},{"--unknown"}}) {
            flags.push_back("--json");r=run(executable,flags,box.dir);check(r.code==2&&!one_json(r)["success"].get<bool>(),"invalid CLI args");
        }
        r=run(executable,{},box.dir,"/open \""+db.string()+"\"\n/tables\n/schema\n/model\n/verbose on\n/oops\n/exit\n");
        check(r.code==0&&r.out.find("Connected")!=std::string::npos&&r.out.find("Goodbye")!=std::string::npos&&r.err.find("Unknown command")!=std::string::npos,"REPL commands");
        r=run(executable,{},box.dir);check(r.code==0,"EOF");
        r=run(executable,{},box.dir,"",true,true);check(r.code==130,"Ctrl+C at REPL prompt");
        r=run(executable,{"--json"},box.dir,"/open "+db.string()+"\n/tables\n/schema\n/exit\n");
        std::istringstream lines(r.out);std::string line;int count=0;while(std::getline(lines,line)){check(Json::parse(line).is_object(),"REPL JSON object");++count;}
        check(count==4&&r.code==0,"JSON REPL documents");
        // Environment overrides config; only fake values are used.
        {std::ofstream config(box.dir/"config.json");config<<Json{{"api",{{"api_key","wrong-config-key"}}}};}
        setenv("DEEPSEEK_API_KEY","fake-secret",1);setenv("T2S_FAKE_MODE","agent",1);
        auto capture_file=(box.dir/"capture.json").string();setenv("T2S_CAPTURE_FILE",capture_file.c_str(),1);
        auto cleaned=[&](){
            auto saved=Json::parse(bytes(capture_file));
            check(!fs::exists(saved["temporary_directory"].get<std::string>()),"HTTP temporary files cleaned");
            errno=0;check(kill(saved["pid"].get<int>(),0)==-1&&errno==ESRCH,"HTTP child reaped");
        };
        r=run(executable,{"query","--db",db.string(),"--question","count","--json","--verbose"},box.dir);j=one_json(r);
        check(r.code==0&&j["success"]&&j["sql_attempts"]==2&&j["usage"]["total_tokens"]==75&&j["query"]["rows"][0][0]==2,"API full roundtrip with repair");
        check(r.out.find("fake-secret")==std::string::npos&&r.err.find("fake-secret")==std::string::npos,"no key in logs");
        cleaned();
        for(const auto& mode:{"http_error","bad_json","bad_envelope","transport","timeout","huge"}) {
            setenv("T2S_FAKE_MODE",mode,1);r=run(executable,{"query","--db",db.string(),"--question","count","--json"},box.dir);j=one_json(r);
            check(r.code==1&&j["termination"]=="api_error","API failure: "+std::string(mode));
            check(r.out.find("fake-secret")==std::string::npos,"error response redaction");
            cleaned();
        }
        setenv("T2S_FAKE_MODE","sleep",1);r=run(executable,{"query","--db",db.string(),"--question","count","--json"},box.dir,"",true);
        check(r.code==130&&one_json(r)["termination"]=="cancelled","SIGINT cancels curl and agent");
        cleaned();
        {std::ofstream config(box.dir/"short-timeout.json");config<<Json{{"api",{{"timeout_seconds",1}}}};}
        r=run(executable,{"query","--db",db.string(),"--question","count","--config",(box.dir/"short-timeout.json").string(),"--json"},box.dir);
        check(r.code==1&&one_json(r)["error"].get<std::string>().find("timed out")!=std::string::npos,"parent HTTP deadline");cleaned();
        setenv("T2S_FAKE_MODE","agent",1);
        auto dataset=box.dir/"spider";auto directory=dataset/"database"/"fixture";fs::create_directories(directory);fs::copy_file(db,directory/"fixture.sqlite");
        Json examples=Json::array({Json{{"db_id","fixture"},{"question","Count employees"},{"query","SELECT COUNT(*) FROM employees"}},
            Json{{"db_id","fixture"},{"question","Count employees"},{"query","SELECT 1"}},
            Json{{"db_id","fixture"},{"question","Count employees"},{"query","DELETE FROM employees"}}});
        {std::ofstream dev(dataset/"dev.json");dev<<examples;}
        auto output=box.dir/"report.jsonl";
        r=run(executable,{"benchmark","--dataset",dataset.string(),"--mode","both","--limit","3","--output",output.string(),"--json"},box.dir);j=one_json(r);
        check(r.code==0&&j["modes"].size()==2,"benchmark both modes");
        for(auto mode:{"baseline","agentic"}) {
            const auto& stats=j["modes"][mode];
            check(stats["cases"]==3&&stats["execution_successes"]==3&&stats["result_matches"]==1&&stats["comparable_cases"]==2,"benchmark match denominator");
            auto predictions=bytes(j["predictions"][mode].get<std::string>());
            check(std::count(predictions.begin(),predictions.end(),'\n')==3&&predictions.find("\tfixture")!=std::string::npos,"prediction export");
        }
        std::istringstream records(bytes(output));count=0;while(std::getline(records,line)){check(Json::parse(line).is_object(),"JSONL object");++count;}check(count==8,"JSONL records and summary");
        r=run(executable,{"benchmark","--dataset",dataset.string(),"--output",output.string(),"--json"},box.dir);check(r.code==1,"existing output preserved");
        setenv("T2S_FAKE_MODE","http_error",1);
        r=run(executable,{"benchmark","--dataset",dataset.string(),"--limit","3","--output",(box.dir/"failed.jsonl").string(),"--json"},box.dir);
        j=one_json(r);check(r.code==1&&j["modes"]["agentic"]["cases"]==1&&!j["success"].get<bool>(),"benchmark stops on API failure");
        setenv("T2S_FAKE_MODE","agent",1);
        Json traversal=Json::array({Json{{"db_id","../outside"},{"question","count"},{"query","SELECT 1"}}});
        {std::ofstream dev(dataset/"dev.json");dev<<traversal;}
        r=run(executable,{"benchmark","--dataset",dataset.string(),"--output",(box.dir/"traversal.jsonl").string(),"--json"},box.dir);
        j=one_json(r);check(r.code==1&&j["modes"]["agentic"]["execution_successes"]==0&&j["modes"]["agentic"]["case_errors"]==1,"db_id traversal rejected");
        check(bytes(db)==before&&bytes(directory/"fixture.sqlite")==before,"all source databases unchanged");
        {std::ofstream config(box.dir/"bad-config.json");config<<"{\"api\":{\"api_key\":\"fake-secret\",\"timeout_seconds\":0}}";}
        r=run(executable,{"demo","--config",(box.dir/"bad-config.json").string(),"--json"},box.dir);check(r.code==2&&r.out.find("fake-secret")==std::string::npos,"config bounds and redaction");
        std::cout<<"Passed "<<checks<<" integration checks (mock HTTP, Spider-format fixture; no live API)\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
}
