#include "agent.h"
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
    auto repaired=run_agent(*company,"top salaries",mock_demo(true));
    check(repaired["success"]&&repaired["sql_attempts"]==2&&repaired["repairs"]==1,"repair agent");
    AgentOptions few;few.max_steps=2;
    check(run_agent(*company,"top salaries",mock_demo(),few)["termination"]=="step_limit","step limit");
    Completion fail=[](const Json&,const Json&){return Json{{"message",{{"role","assistant"},{"content",nullptr},{"tool_calls",{tool_call("id"+std::to_string(checks++),"execute_sql",{{"sql","SELECT nonexistent FROM employees"}})}}}}};};
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
    bool bad_json=false;try{parse_completion_text("not json");}catch(...){bad_json=true;}check(bad_json,"bad JSON");
    auto body=request_body(empty,Json::array(),agent_tools());
    check(body["stream"]==false&&body["thinking"]["type"]=="disabled"&&body["tool_choice"]=="auto","request API parameters");
    Completion baseline=[](const Json&,const Json& tools){check(tools.empty(),"baseline no tools");return Json{{"message",{{"role","assistant"},{"content","```sql\nSELECT COUNT(*) FROM employees\n```"}}},{"usage",{{"total_tokens",7}}}};};
    auto base=run_baseline(*company,"count",baseline);check(base["success"]&&base["usage"]["total_tokens"]==7,"baseline");
    std::cout<<"Passed "<<checks<<" checks\n"; return 0;
 } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
}
