# 课程精简版的代码阅读

> 用途：帮助阅读当前 C++ 源码，重点是模块分工、结构体、简单指针和内存释放。启动与 API 配置见 [仓库 README](../../README.md)。

保留 JSON 配置、Windows/macOS/Linux、SQLite、真实 AI 和 CSV Benchmark。代码使用普通函数、结构体、数组、判断、循环和简单指针，没有自写头文件或测试目录。

## 核心流程

先从 `agent.cpp` 的 `run_query` 阅读：

```cpp
for (int i = 0; i < attempts; i++) {
    if (cancelled) {
        break;
    }
    TextResult sql = request_sql(source);
    if (sql.success == false) {
        break;
    }

    free_query(&result.query);
    result.query = execute_sql(db, sql.text);
    QueryResult* query = &result.query;
    if (query->success) {
        break;
    }

    add_message(source, "user", "执行失败，请修正上一条 SQL。");
}
```

这是流程节选。实际函数还会检查结构读取、请求错误和次数上限；完整实现位于 `src/agent.cpp`。Baseline 传入 1，纠错模式最多传入 3。

AI 只返回 SQL。表结构由程序提前读取，不再让模型选择工具；成功后直接显示 SQLite 结果，不再请求 AI 解释或生成 trace。

## 结果结构体和简单指针

```cpp
QueryResult result = execute_sql(&db, "SELECT name FROM employees");
if (result.success) {
    print_table(&result);
} else {
    cout << result.error << endl;
}
free_query(&result);
```

`&db` 和 `&result` 是取得结构体地址。函数通过 `Database*`、`QueryResult*` 使用同一份数据；`result->row_count` 读取指针所指结构体的字段。

查询行由动态数组保存。函数返回后，数组交给调用方；之后通过指针借用，最终释放一次。不要把拥有行数组的结构体复制成两个长期保存的变量。

| 创建的内容 | 使用后释放 |
|---|---|
| `execute_sql`、`list_tables` 的结果 | `free_query(&result)` |
| `run_query` 的最终查询结果 | `free_query(&result.query)` |
| SQLite 连接 | `close_database(&db)` |

行索引数组按本次行数上限一次分配，不再扩容或搬移；每行单元格按实际读取的数据分配。这样保留本地查询和十万行评测能力，又不提前创建海量单元格。`new(nothrow)` 和 `delete[]` 只用于必要的分配、释放；单次初始分配仍可能失败，函数会返回错误。

多层访问拆成容易理解的局部指针：

```cpp
QueryRow* row = &result->rows[r];
Cell* cell = &row->cells[c];
cout << cell_text(cell);
```

Benchmark 用归并排序排序行号，不复制整行字符串。

## 文件分工

| 文件 | 内容 |
|---|---|
| `main.cpp` | JSON 配置、查询入口、Windows UTF-8 参数转换 |
| `common.cpp` | 普通结构体和少量共用函数 |
| `agent.cpp` | 固定 SQL 生成与纠错循环 |
| `database.cpp` | SQLite 连接、表结构、执行限制与结果数组 |
| `llm.cpp` | HTTPS 请求及 SQL 回复解析 |
| `json.cpp` | JSON 字符串转义及协议辅助函数 |
| `config.cpp` | JSON 配置和环境变量 |
| `benchmark.cpp` | 两种模式、结果比较、CSV 与汇总 |
| `files.cpp` | 文件、路径和数据集读取 |
| `cli.cpp` | 自然语言输入和终端输出 |
| `demo.cpp` | 内置 SQLite 示例 |

各模块由 `main.cpp` 统一包含；不要再次把它们单独加入同一个程序的编译列表。第三方和系统库的头文件继续保留。

## 写法约定

变量分别声明，所有 `if/else/for/while` 使用大括号分行。复杂条件拆成小步骤，基础 `&&` 和 `||` 仍用于简单的两项判断。自写代码不使用类定义、引用参数、异常、模板定义、Lambda、智能指针、`vector/map/set`、`auto`、三元表达式或 `.get<T>()`。

JSON 的底层库操作集中在 `json.cpp`。配置和数据集读取使用 `read_json_string()`、`read_json_number()` 等普通函数：

```cpp
TextResult value = read_json_string(object, "model");
if (value.success) {
    model = value.text;
} else {
    cout << value.error << endl;
}
```

业务路径用字符串和 `join_path()` 拼接，路径规范化和中文跨平台细节保留在 `files.cpp`。类型转换及 `#ifdef _WIN32` 等条件编译按用户选择保留，Windows 参数和控制台适配不变。启动命令见 README。

默认启动显示自然语言输入和引导，数据库从 `--db` 或 JSON 的 `database.path` 连接；未提供时默认连接随仓库提供的样例数据库。`/demo` 离线体验，`/help` 查看引导，`/exit` 退出。Benchmark 和 Demo 使用独立启动命令，见 README。

样例库和压缩后的完整 Spider 在仓库 `data/`，CMake 自动解压到构建目录。Benchmark 的数据集路径留空即可使用内置数据；没有 API Key 时仍可以直接运行离线 Demo。
