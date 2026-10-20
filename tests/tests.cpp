#include "database.h"
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
    std::cout<<"Passed "<<checks<<" checks\n"; return 0;
 } catch(const std::exception& e) { std::cerr<<"FAIL: "<<e.what()<<'\n';return 1; }
}
