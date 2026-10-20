PRAGMA foreign_keys=ON;
CREATE TABLE departments(id INTEGER PRIMARY KEY, name TEXT NOT NULL);
CREATE TABLE employees(id INTEGER PRIMARY KEY, name TEXT NOT NULL, department_id INTEGER REFERENCES departments(id), salary REAL, note TEXT);
INSERT INTO departments VALUES (1,'Engineering'),(2,'Finance'),(3,'Marketing');
INSERT INTO employees VALUES
 (1,'Alice',1,120000,NULL), (2,'Bob',2,90000,'Part time'),
 (3,'Carol',3,80000,NULL), (4,'David',1,110000,NULL),
 (5,'李明',2,100000,'中文示例'), (6,'Eve',3,85000,NULL);
