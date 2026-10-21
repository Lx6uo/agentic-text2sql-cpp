#include "agent.h"
#include "benchmark.h"
#include <iostream>
#include <stdexcept>
using namespace t2s;
int checks=0;
void check(bool ok, const char* message) {++checks; if(!ok) throw std::runtime_error(message);}
int main() {
 try {
    auto db=Database::from_sql("CREATE TABLE d(id INTEGER PRIMARY KEY,name TEXT); CREATE TABLE e(id INTEGER,name TEXT,did INTEGER REFERENCES d(id), salary REAL); INSERT INTO d VALUES(1,'Engineering'),(2,'Finance'); INSERT INTO e VALUES(1,'Alice',1,120),(2,'Bob',2,NULL),(3,'Carol',1,100);");
    check(db->list_tables().size()==2,"tables");
    check(db->schema()[1]["foreign_keys"].size()==1,"foreign keys");
    check(db->execute("SELECT name FROM e ORDER BY id")["rows"].size()==3,"select");
    check(db->execute("SELECT e.name,d.name FROM e JOIN d ON e.did=d.id")["success"],"join");
    check(db->execute("SELECT avg(salary) FROM e")["rows"][0][0]==110.0,"aggregate");
    check(db->execute("SELECT salary FROM e WHERE id=2")["rows"][0][0].is_null(),"null");
    check(db->execute("SELECT * FROM e",1)["truncated"],"truncate");
    for(auto sql:{"SELEC name FROM e","SELECT missing FROM e","INSERT INTO e VALUES(4,'X',1,1)","UPDATE e SET name='X'","DELETE FROM e","DROP TABLE e","CREATE TABLE x(a)","ALTER TABLE e ADD x","ATTACH ':memory:' AS x","DETACH main","PRAGMA table_info(e)","PRAGMA query_only=OFF","SELECT 1; SELECT 2","SELECT load_extension('x')","SELECT randomblob(1000000)","BEGIN"})
        check(!db->execute(sql)["success"],sql);
    check(db->execute("SELECT 1; -- comment\n")["success"],"trailing comment");
    check(!db->execute(std::string(17000,' '))["success"],"sql length");
    check(!db->execute(std::string("SELECT 1\0;DELETE FROM e",23))["success"],"nul");
    DatabaseOptions limits;limits.timeout_ms=5;
    auto slow=Database::from_sql("",limits);
    check(slow->execute("WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n) SELECT sum(x) FROM n")["timed_out"],"timeout");
    cancelled=1;check(db->execute("SELECT 1")["cancelled"],"cancel");cancelled=0;
    bool missing=false;try{Database bad("/no/such/text2sql.sqlite");}catch(...){missing=true;}check(missing,"missing path");
    auto company=Database::from_sql("CREATE TABLE departments(id INTEGER PRIMARY KEY,name TEXT); CREATE TABLE employees(id INTEGER PRIMARY KEY,name TEXT,department_id INTEGER REFERENCES departments(id),salary REAL); INSERT INTO departments VALUES(1,'Engineering'); INSERT INTO employees VALUES(1,'Alice',1,100),(2,'Bob',1,80);");
    auto normal=run_agent(*company,"top salaries",mock_demo());
    check(normal["success"]&&normal["sql_attempts"]==1,"normal agent");
    auto schema_only=[step=0](const Json&,const Json&)mutable {
        Json message{{"role","assistant"},{"content",nullptr}};
        if(step++==0)message["tool_calls"]={tool_call("tables-only","list_tables",Json::object())};
        else message["content"]="The database has departments and employees.\nBoth are tables.";
        return Json{{"message",message}};
    };
    auto schema_answer=run_agent(*company,"What tables exist?",schema_only);
    check(schema_answer["success"]&&schema_answer["termination"]=="completed"&&schema_answer["sql_attempts"]==0,"schema-only answer succeeds");
    auto repaired=run_agent(*company,"top salaries",mock_demo(true));
    check(repaired["success"]&&repaired["sql_attempts"]==2&&repaired["repairs"]==1,"repair agent");
    AgentOptions few;few.max_steps=2;
    check(run_agent(*company,"top salaries",mock_demo(),few)["termination"]=="step_limit","step limit");
    Completion fail=[sequence=0](const Json&,const Json&)mutable{return Json{{"message",{{"role","assistant"},{"content",nullptr},{"tool_calls",{tool_call("id"+std::to_string(sequence++),"execute_sql",{{"sql","SELECT nonexistent FROM employees"}})}}}}};};
    auto repeated=run_agent(*company,"bad",fail);
    check(!repeated["success"]&&repeated["sql_attempts"]==3&&repeated["termination"]=="sql_attempt_limit","attempt limit");
    AgentOptions no_repair;no_repair.max_repairs=0;
    check(run_agent(*company,"bad",fail,no_repair)["termination"]=="repair_limit","repair limit");
    auto invalid=[count=0](const Json& messages,const Json&)mutable {
        if(count++==0)return Json{{"message",{{"role","assistant"},{"content",nullptr},{"tool_calls",{tool_call("x","unknown",Json::object()),tool_call("y","execute_sql",Json::object()),tool_call("z","describe_tables",{{"tables",{1}}})}}}}};
        check(messages.size()==6,"all tool results in context");
        for(size_t i=3;i<6;++i)check(messages[i]["role"]=="tool"&&messages[i].contains("tool_call_id"),"tool_call_id context");
        return Json{{"message",{{"role","assistant"},{"content","Cannot answer"}}}};
    };
    auto bad=run_agent(*company,"bad",invalid);check(bad["termination"]=="unable_to_answer"&&bad["trace"].size()==3,"invalid calls bounded");
    Completion malformed=[](const Json&,const Json&){return Json{{"message",{{"role","assistant"},{"tool_calls",{Json{{"type","function"}}}}}}};};
    check(run_agent(*company,"bad",malformed)["termination"]=="api_error","malformed API envelope");
    ApiOptions empty;
    check(run_agent(*company,"q",deepseek(empty))["error"].get<std::string>().find("Missing API key")!=std::string::npos,"missing API key");
    ApiOptions insecure;insecure.api_key="fake-key";insecure.base_url="http://api.example.com";
    bool insecure_rejected=false;try{curl_post(insecure,Json::object());}catch(...){insecure_rejected=true;}check(insecure_rejected,"non-HTTPS API rejected");
    bool bad_json=false;try{parse_completion_text("not json");}catch(...){bad_json=true;}check(bad_json,"bad JSON");
    auto body=request_body(empty,Json::array(),agent_tools());
    check(body["stream"]==false&&body["thinking"]["type"]=="disabled"&&body["tool_choice"]=="auto","request API parameters");
    Completion baseline=[](const Json&,const Json& tools){check(tools.empty(),"baseline no tools");return Json{{"message",{{"role","assistant"},{"content","```sql\nSELECT COUNT(*) FROM employees\n```"}}},{"usage",{{"total_tokens",7}}}};};
    auto base=run_baseline(*company,"count",baseline);check(base["success"]&&base["usage"]["total_tokens"]==7,"baseline");
    auto gold=company->execute("SELECT name,salary FROM employees ORDER BY name");
    auto reversed=company->execute("SELECT name,salary FROM employees ORDER BY name DESC");
    check(equivalent_results(gold,reversed,false),"unordered equivalence");
    check(!equivalent_results(gold,reversed,true),"ordered difference");
    check(!equivalent_results(company->execute("SELECT salary,name FROM employees"),gold,false),"positional columns");
    check(!equivalent_results(company->execute("SELECT 1 UNION ALL SELECT 1"),company->execute("SELECT 1 UNION ALL SELECT 2"),false),"duplicate multiplicity");
    check(equivalent_results(company->execute("SELECT 1"),company->execute("SELECT 1.0"),false),"numeric equivalence");
    check(!equivalent_results(company->execute("SELECT '1'"),company->execute("SELECT 1"),false),"text numeric difference");
    auto trunc=gold;trunc["truncated"]=true;check(!equivalent_results(trunc,gold,false),"truncated not comparable");
    check(has_outer_order_by("SELECT x FROM (SELECT 1 AS x ORDER BY x) ORDER /* comment */ BY x"),"outer ordering");
    check(!has_outer_order_by("SELECT 'ORDER BY' FROM (SELECT 1 ORDER BY 1) -- ORDER BY\n"),"inner ordering ignored");
    check(!company->execute("SELECT * FROM pragma_table_info('employees')")["success"],"table valued pragma denied");
    check(!company->execute("VACUUM INTO '/tmp/text2sql-should-not-exist.sqlite'")["success"],"vacuum denied");
    check(!company->execute("SELECT writefile('/tmp/text2sql-should-not-exist.txt', 'x')")["success"],"file functions denied");
    check(company->execute("WITH x AS (SELECT COUNT(*) AS n FROM employees) SELECT n FROM x")["success"],"CTE allowed");
    check(company->execute("SELECT ROW_NUMBER() OVER (ORDER BY salary) FROM employees")["success"],"window allowed");
    DatabaseOptions tiny;tiny.max_result_bytes=20;
    auto capped=Database::from_sql("CREATE TABLE t(x TEXT); INSERT INTO t VALUES('long enough to exceed byte limit');",tiny);
    check(capped->execute("SELECT x FROM t")["truncated"],"result bytes limit");
    auto unusual=Database::from_sql("CREATE TABLE \"sqliteX\"(\"a\"\"b\" INTEGER PRIMARY KEY); CREATE TABLE \"订单 明细\"(\"商品名\" TEXT); INSERT INTO \"订单 明细\" VALUES('Apple');");
    check(unusual->list_tables().size()==2,"sqliteX table name allowed");
    check(unusual->describe_tables({"sqliteX"})[0]["columns"][0]["name"]=="a\"b","quoted identifier schema");
    check(unusual->execute("SELECT \"商品名\" FROM \"订单 明细\"")["rows"][0][0]=="Apple","unicode arbitrary schema");
    bool injection=false;try{unusual->describe_tables({"sqliteX\"); DROP TABLE sqliteX; --"});}catch(...){injection=true;}check(injection&&unusual->list_tables().size()==2,"schema identifier injection rejected");
    check(!company->execute("SELECT \"not_a_column\" FROM employees")["success"],"double quoted typo denied");
    DatabaseOptions reference_options;reference_options.reference_double_quoted_strings=true;
    auto ref=Database::from_sql("",reference_options);check(ref->execute("SELECT \"historical string literal\"")["success"],"reference DQS compatibility");
    auto arbitrary=[step=0](const Json& messages,const Json&)mutable {
        Json m{{"role","assistant"},{"content",nullptr}};
        if(step==0)m["tool_calls"]={tool_call("s","describe_tables",{{"tables",{"订单 明细"}}})};
        else if(step==1) {
            check(messages.back()["content"].get<std::string>().find("商品名")!=std::string::npos,"actual arbitrary schema in context");
            m["tool_calls"]={tool_call("q","execute_sql",{{"sql","SELECT \"商品名\" FROM \"订单 明细\""}})};
        }else m["content"]="Apple";
        ++step;return Json{{"message",m}};
    };
    auto arbitrary_result=run_agent(*unusual,"商品有哪些",arbitrary);
    check(arbitrary_result["success"]&&arbitrary_result["query"]["rows"][0][0]=="Apple","agent arbitrary schema and different tool order");
    auto invalid_json_args=[step=0](const Json&,const Json&)mutable {
        Json m{{"role","assistant"},{"content",nullptr}};
        if(step++==0) {
            auto call=tool_call("bad-args","execute_sql",Json::object());call["function"]["arguments"]="{";m["tool_calls"]={call};
        }else m["content"]="Cannot answer";
        return Json{{"message",m}};
    };
    check(run_agent(*company,"q",invalid_json_args)["trace"][0]["success"]==false,"invalid tool argument JSON");
    Json cut_response;cut_response["choices"]=Json::array({Json{{"finish_reason","length"},{"message",{{"role","assistant"},{"content","SELECT"}}}}});
    bool incomplete=false;try{parse_completion(cut_response);}catch(...){incomplete=true;}check(incomplete,"incomplete API completion rejected");
    std::cout<<"Passed "<<checks<<" checks\n"; return 0;
 } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
}
