#include "benchmark.h"
#include <algorithm>
#include <cmath>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <set>
#include <stdexcept>
#include <unistd.h>

namespace t2s {
namespace fs=std::filesystem;
namespace {
class Output {
    int fd_=-1;
public:
    explicit Output(const fs::path& path) {
        if(!path.parent_path().empty())fs::create_directories(path.parent_path());
        fd_=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
        if(fd_<0)throw std::runtime_error("Cannot create output (existing files are preserved): "+path.string());
    }
    ~Output(){if(fd_>=0)close(fd_);}
    void write(const std::string& s) {
        size_t n=0;while(n<s.size()) {
            auto wrote=::write(fd_,s.data()+n,s.size()-n);
            if(wrote<0&&errno==EINTR)continue;
            if(wrote<=0)throw std::runtime_error("Benchmark output write failed");
            n+=static_cast<size_t>(wrote);
        }
    }
};
bool cell_equal(const Json& a,const Json& b) {
    if(a.is_number()&&b.is_number()) {
        long double x=a.get<long double>(),y=b.get<long double>();
        return x==y||std::abs(x-y)<=1e-9L*std::max({1.0L,std::abs(x),std::abs(y)});
    }
    return a.type()==b.type()&&a==b;
}
bool rows_equal(const Json& a,const Json& b,const std::vector<size_t>& columns,bool ordered) {
    auto same=[&](const Json& x,const Json& y){for(size_t c=0;c<columns.size();++c)if(!cell_equal(x[columns[c]],y[c]))return false;return true;};
    if(ordered){for(size_t r=0;r<a.size();++r)if(!same(a[r],b[r]))return false;return true;}
    // Sort row indices to preserve duplicates without quadratic matching cost.
    auto less_cell=[](const Json& x,const Json& y) {
        if(x.is_number()&&y.is_number())return x.get<long double>()<y.get<long double>();
        if(x.type()!=y.type())return static_cast<int>(x.type())<static_cast<int>(y.type());
        return x<y;
    };
    std::vector<size_t> ai(a.size()),bi(b.size());std::iota(ai.begin(),ai.end(),0);std::iota(bi.begin(),bi.end(),0);
    auto less_row=[&](const Json& x,const Json& y,bool prediction) {
        for(size_t c=0;c<columns.size();++c) {
            const auto& xc=x[prediction?columns[c]:c];const auto& yc=y[prediction?columns[c]:c];
            if(less_cell(xc,yc))return true;if(less_cell(yc,xc))return false;
        }
        return false;
    };
    std::sort(ai.begin(),ai.end(),[&](size_t x,size_t y){return less_row(a[x],a[y],true);});
    std::sort(bi.begin(),bi.end(),[&](size_t x,size_t y){return less_row(b[x],b[y],false);});
    for(size_t r=0;r<ai.size();++r)if(!same(a[ai[r]],b[bi[r]]))return false;
    return true;
}
bool contained(const fs::path& path,const fs::path& root) {
    auto p=path.begin();for(auto r=root.begin();r!=root.end();++r,++p)if(p==path.end()||*p!=*r)return false;return true;
}
fs::path database_path(const fs::path& root,const std::string& id) {
    if(id.empty()||id.size()>128||id=="."||id==".."||
       !std::all_of(id.begin(),id.end(),[](unsigned char c){return std::isalnum(c)||c=='_'||c=='-';}))
        throw std::runtime_error("Unsafe Spider db_id");
    auto p=root/"database"/id/(id+".sqlite");
    if(!fs::exists(p))throw std::runtime_error("Spider database not found: "+id);
    auto canonical=fs::canonical(p);
    if(!contained(canonical,root))throw std::runtime_error("Spider database escapes dataset directory");
    return canonical;
}
}
bool has_outer_order_by(const std::string& sql) {
    int depth=0;char quote=0;std::string token,last;
    auto flush=[&]() {if(token.empty())return false;bool found=depth==0&&last=="ORDER"&&token=="BY";last=depth==0?token:"";token.clear();return found;};
    for(size_t i=0;i<sql.size();++i) {
        char c=sql[i];
        if(quote) {
            if(c==quote){if(i+1<sql.size()&&sql[i+1]==quote)++i;else quote=0;}
            continue;
        }
        if(c=='\''||c=='"'||c=='`'||c=='['){if(flush())return true;quote=c=='['?']':c;last.clear();continue;}
        if(c=='-'&&i+1<sql.size()&&sql[i+1]=='-'){if(flush())return true;while(i<sql.size()&&sql[i]!='\n')++i;continue;}
        if(c=='/'&&i+1<sql.size()&&sql[i+1]=='*'){if(flush())return true;i+=2;while(i+1<sql.size()&&!(sql[i]=='*'&&sql[i+1]=='/'))++i;++i;continue;}
        if(std::isalnum(static_cast<unsigned char>(c))||c=='_')token+=static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        else {if(flush())return true;if(c=='('){++depth;last.clear();}else if(c==')'){--depth;last.clear();}else if(!std::isspace(static_cast<unsigned char>(c)))last.clear();}
    }
    return flush();
}
bool equivalent_results(const Json& p,const Json& g,bool ordered) {
    if(!p.value("success",false)||!g.value("success",false)||p.value("truncated",false)||g.value("truncated",false))return false;
    const auto& a=p.at("rows");const auto& b=g.at("rows");
    size_t columns=g.at("columns").size();
    if(p.at("columns").size()!=columns||a.size()!=b.size())return false;
    std::vector<size_t> map(columns);std::iota(map.begin(),map.end(),0);
    return rows_equal(a,b,map,ordered);
}
Json run_benchmark(const BenchmarkOptions& o,const Config& config,Completion complete) {
    if(o.limit<1)throw std::runtime_error("Benchmark limit must be positive");
    if(o.mode!="baseline"&&o.mode!="agentic"&&o.mode!="both")throw std::runtime_error("Invalid benchmark mode");
    if(o.split!="dev"&&o.split!="train"&&o.split!="train_spider"&&o.split!="train_others")throw std::runtime_error("Unsupported split: use dev, train_spider or train_others");
    auto root=fs::canonical(o.dataset);
    std::string split=o.split=="train"?"train_spider":o.split;
    auto file=root/(split+".json");
    if(!fs::exists(file))throw std::runtime_error("Spider questions not found: "+file.string());
    if(!contained(fs::canonical(file),root))throw std::runtime_error("Spider question file escapes dataset directory");
    if(fs::file_size(file)>64*1024*1024)throw std::runtime_error("Spider question file exceeds 64 MB");
    std::ifstream in(file);Json examples;
    try{in>>examples;}catch(...){throw std::runtime_error("Invalid Spider questions JSON");}
    if(!examples.is_array())throw std::runtime_error("Spider split must be a JSON array");
    size_t count=std::min(examples.size(),static_cast<size_t>(o.limit));
    if(count==0)throw std::runtime_error("Empty Spider split");
    // Validate required fields before creating artifacts or making API calls.
    for(size_t i=0;i<count;++i) {
        auto& e=examples[i];
        if(!e.is_object()||!e.contains("db_id")||!e["db_id"].is_string()||!e.contains("question")||!e["question"].is_string()||!e.contains("query")||!e["query"].is_string())
            throw std::runtime_error("Spider item missing db_id, question or query at index "+std::to_string(i));
    }
    auto stamp=std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    fs::path output=o.output.empty()?fs::path("results")/("spider-"+stamp+".jsonl"):fs::path(o.output);
    fs::path pred_base=o.predictions.empty()?fs::path(output.string()+".sql"):fs::path(o.predictions);
    std::vector<std::string> modes=o.mode=="both"?std::vector<std::string>{"baseline","agentic"}:std::vector<std::string>{o.mode};
    std::map<std::string,std::unique_ptr<Output>> predictions;
    Json paths=Json::object();
    Output records(output);
    for(auto& mode:modes) {
        auto path=modes.size()==1?pred_base:fs::path(pred_base.string()+"."+mode+".sql");
        predictions[mode]=std::make_unique<Output>(path);paths[mode]=path.string();
    }
    Json metadata{{"type","metadata"},{"dataset","Spider 1.0"},{"split",split},{"model",config.api.model},
        {"requested",count},{"metric","Local execution-result agreement; not official Test Suite Accuracy"},
        {"comparison","Positional columns; duplicate-preserving rows; outer ORDER BY preserves row order; numeric relative tolerance 1e-9. Truncated results are not comparable."},
        {"agent_limits",{{"max_steps",config.agent.max_steps},{"max_sql_attempts",config.agent.max_sql_attempts},{"max_repairs",config.agent.max_repairs}}}};
    records.write(metadata.dump()+"\n");
    struct Stats {int n=0,execution=0,matches=0,comparable=0,completed=0,errors=0;double steps=0,attempts=0,time=0;Json tokens=Json::object();};
    std::map<std::string,Stats> stats;
    std::string abort_reason;
    for(size_t i=0;i<count&&!cancelled&&abort_reason.empty();++i) {
        const auto& example=examples[i];auto id=example["db_id"].get<std::string>();auto question=example["question"].get<std::string>();
        for(const auto& mode:modes) {
            if(cancelled)break;
            auto& s=stats[mode];++s.n;
            Json record{{"type","case"},{"index",i},{"db_id",id},{"question",question},{"mode",mode},
                {"reference_sql",example["query"]},{"predicted_sql",""},{"execution_success",false},{"result_match",nullptr}};
            try {
                auto path=database_path(root,id);
                Database db(path.string(),config.database);
                // The reference SQL is never passed to complete / agent.
                auto run=mode=="baseline"?run_baseline(db,question,complete,o.verbose):run_agent(db,question,complete,config.agent,o.verbose);
                record["agent"]=run; s.steps+=run["agent_steps"].get<int>();s.attempts+=run["sql_attempts"].get<int>();s.time+=run["elapsed_ms"].get<double>();
                if(run["termination"]=="api_error")abort_reason=run["error"].get<std::string>();
                if(run["success"].get<bool>())++s.completed;
                for(auto& [key,value]:run["usage"].items())if(value.is_number_integer())s.tokens[key]=s.tokens.value(key,int64_t(0))+value.get<int64_t>();
                auto query=run["query"];
                if(!query.is_null()) {
                    record["predicted_sql"]=query["sql"];
                    record["execution_success"]=query["success"];
                    if(query["success"].get<bool>())++s.execution;
                }
                DatabaseOptions evaluation=config.database; evaluation.max_display_rows=100000;evaluation.max_result_bytes=16*1024*1024;
                Database evaluate(path.string(),evaluation);
                DatabaseOptions reference_options=evaluation;reference_options.reference_double_quoted_strings=true;
                Database reference(path.string(),reference_options);
                auto gold=reference.execute(example["query"].get<std::string>());
                record["reference_success"]=gold["success"];record["reference_error"]=gold["error"];record["reference_truncated"]=gold["truncated"];
                if(!query.is_null()&&query["success"].get<bool>()&&gold["success"].get<bool>()) {
                    auto predicted=evaluate.execute(query["sql"].get<std::string>());
                    record["prediction_truncated"]=predicted["truncated"];record["evaluation_error"]=predicted["error"];
                    bool comparable=predicted["success"].get<bool>()&&!predicted["truncated"].get<bool>()&&!gold["truncated"].get<bool>();
                    record["predicted_rows"]=predicted["rows"].size();record["reference_rows"]=gold["rows"].size();
                    if(comparable) {++s.comparable;bool match=equivalent_results(predicted,gold,has_outer_order_by(example["query"].get<std::string>()));record["result_match"]=match;if(match)++s.matches;}
                }
            }catch(const std::exception& e){record["error"]=e.what();++s.errors;}
            records.write(record.dump(-1,' ',false,Json::error_handler_t::replace)+"\n");
            auto sql=record["predicted_sql"].get<std::string>();for(char& c:sql)if(c=='\n'||c=='\r'||c=='\t')c=' ';
            predictions[mode]->write(sql+"\t"+id+"\n");
            std::cerr<<"[benchmark "<<mode<<" "<<i+1<<"/"<<count<<"] "<<id<<": execution="<<record["execution_success"]<<", match="<<record["result_match"]<<'\n';
            if(!abort_reason.empty())break;
        }
    }
    Json summary{{"type","summary"},{"success",!cancelled&&abort_reason.empty()},{"cancelled",cancelled!=0},{"requested_cases_per_mode",count},{"error",abort_reason.empty()?Json(nullptr):Json(abort_reason)},{"output",output.string()},{"predictions",paths},
        {"metric",metadata["metric"]},{"modes",Json::object()}};
    for(const auto& mode:modes) {
        const auto& s=stats[mode];double n=s.n?s.n:1;
        summary["modes"][mode]={{"cases",s.n},{"case_errors",s.errors},{"execution_successes",s.execution},{"result_matches",s.matches},{"comparable_cases",s.comparable},
            {"execution_success_rate",s.execution/n},{"local_result_agreement_rate",s.matches/n},{"agent_completion_rate",s.completed/n},
            {"average_steps",s.steps/n},{"average_sql_attempts",s.attempts/n},{"average_elapsed_ms",s.time/n},{"token_usage",s.tokens}};
        if(s.errors>0)summary["success"]=false;
    }
    records.write(summary.dump()+"\n");return summary;
}
}
