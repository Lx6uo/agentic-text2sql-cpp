# 汇报文档：基于 C++17 的 SQLite 自然语言查询与结果分析

本文用于项目报告、课程展示和 3～5 分钟现场汇报。使用方式见 [README](../README.md)，实现细节见[项目设计与技术文档](design-and-implementation.md)。本文已同步 2026-11-09 的评测工具移除；2026-11-02 的验证结果保留为历史记录，不代表本次重新运行真实模型。

**摘要：** 面向跨数据库 Text-to-SQL 任务，本项目采用 C++17、SQLite、cJSON 和 libcurl，实现自然语言查询与结果分析的终端工具。程序先读取真实数据库结构，调用模型生成一条只读 SQL，先显示执行结果，再用独立请求分析有限结果并显示文字。默认大学数据库 `college_2` 直接纳入仓库，完整 Spider ZIP 原样保留；JSON 配置与 Windows/macOS/Linux 构建适配继续保留。项目已移除自动评测工具、ZIP 自动处理和安装逻辑，采用文档记录问题、预期和实际结果。历史验证确认了真实查询、输入标记与两题样例流程，完整数据集性能仍需另行人工评价。

**关键词：** Spider；Text-to-SQL；C++17；SQLite；提示词；结果分析

## 1. 项目背景与意义

### 1.1 自然语言查询任务

数据库使用者通常需要理解表名、字段和关联关系，再编写 SQL 才能取得数据。Text-to-SQL 将入口转为自然语言：

```text
自然语言问题 + 真实表结构 → SQL → 数据库结果 → 面向用户的分析
```

难点包括字段匹配、跨表关联、筛选、聚合、排名以及返回列和行数是否符合问题。SQL 执行成功只说明语法和执行条件满足，语义是否正确还需要对照结果判断。

### 1.2 项目意义

项目让使用者通过自然语言提出数据库问题，并获得可以核对的 SQL、真实查询结果和中文分析。保留这三部分输出，可以帮助使用者理解查询条件、核对数据来源，并区分数据库返回的数据与模型对数据的解释。

从 C++ 项目实践角度，系统把文件与 JSON 配置、网络 API、SQLite 调用、指针资源管理和 CLI 交互串成一个完整应用。基础语法与分模块实现便于阅读，随仓库提供的真实数据库则为重复验证同一查询提供了共同的数据基础。

### 1.3 Spider 与仓库数据

Spider 1.0 原始论文包含 10181 个问题、5693 条 SQL、200 个数据库和 138 个领域。仓库提供的运行包包含以下划分，其中 `train_others` 是额外的辅助训练数据：

| 运行包划分 | 问题数 | 数据库数 |
|---|---:|---:|
| dev | 1034 | 20 |
| train_spider | 7000 | 140 |
| train_others | 1659 | 6 |
| test | 2147 | 40 |

运行包共有 206 个不重复 SQLite 数据库，覆盖学校、电影、体育等不同领域。dev 划分有 1034 题、20 个数据库；标准 SQL 可用于本地人工核对，不发给模型。

完整运行包以约 57 MiB 的普通 Git ZIP 保存，clone 后仍可取得全部归档数据。CMake 不读取、解压或校验 ZIP；默认大学数据库已经单独提供，构建后可直接启动。其他库可手动解压后通过 `--db` 指定。数据库、题目和元信息保留原始字节，上游说明与引用材料在 ZIP 内；原有 [data/SHA256SUMS](../data/SHA256SUMS) 作为数据来源记录保留。

### 1.4 默认体验数据库 college_2

默认库直接以 [college_2.sqlite](../data/college_2.sqlite) 随仓库提供，约 2 MiB。它有 11 张表，各表记录合计 34620 条，主要数据包括：

| 表或数据 | 内容 |
|---|---|
| student | 2000 名学生及所属院系、学分 |
| instructor | 50 名教师及所属院系、工资 |
| course | 200 门课程及学分 |
| department、classroom | 院系、预算、教室等信息 |
| takes、teaches、section | 选课、授课和课程开设 |
| advisor、prereq、time_slot | 导师、先修关系和时间安排 |

这些关系支持计数、分组、JOIN 和 Top-N 等查询。项目另附 company 小型样例及两道中文评测题，用于快速验证流程。

## 2. 设计思路

### 2.1 设计目标与职责划分

目标是在基础 C++ 写法下，实现可阅读、可构建并能连接真实数据库和模型的完整查询流程。

| 目标 | 当前实现 |
|---|---|
| 适配不同数据库 | 提前读取真实表、字段、主键和外键 |
| 使用真实模型 | libcurl 发送 Chat Completions 请求，默认模型为 deepseek-flash |
| 获得真实结果 | SQLite 只读执行模型生成的一条 SQL |
| 解释查询结果 | 第二次独立 API 请求分析问题、SQL 与有限结果预览 |
| 便于体验 | 启动直接显示引导和自然语言输入，默认连接 college_2 |
| 记录评价结果 | 文档保存问题、SQL、预期和实际结果，注明验证范围 |

### 2.2 固定查询与分析流程

程序控制查询步骤，模型分别承担生成 SQL 和解释结果的任务。成功查询的流程如下：

```mermaid
flowchart LR
    A[自然语言问题] --> B[读取结构并填充提示词]
    B --> C[第一次 API：生成 SQL]
    C --> D[SQLite 只读执行]
    D --> E[CLI 显示 SQL 和数据]
    E --> F[第二次 API：分析结果预览]
    F --> G[CLI 显示分析文字]
```

每个有效问题只生成并执行一次 SQL，成功后先显示查询结果，再请求一次结果分析。SQL 执行失败返回统一错误；分析失败仍保留已经显示的 SQL 和数据。项目不包含自动评测命令。

两次请求分别建立上下文：生成阶段使用当前问题和真实表结构；分析阶段使用当前问题、已执行 SQL 和有限结果预览。每个新问题重新建立消息，数据库连接在会话内复用；涉及连续筛选时，使用者需要在新问题中补全条件。

### 2.3 输入、执行与展示边界

SQL 系统提示词约定无查询意图的输入返回固定标记，程序识别后显示引导；`/help` 和 `/exit` 由 CLI 本地处理。分别查看不同表的原始记录时，模型统一返回“表名 + JSON 记录”，保留各表字段和值，满足一条 SQL 的列数要求。

SQLite 执行层使用只读授权与资源上限，合法 JOIN、聚合、CTE 和窗口函数仍可执行。默认 SQL 超时为 5000 ms，本地读取最多 100 行、约 1 MiB 结果；分析默认接收最多 100 行、128 KiB 的结构化数据并标记截断。错误按少量类别集中处理，结果中保留成功、失败和截断状态，便于用户核对。

## 3. 实现方法

### 3.1 技术栈与源码组织

代码使用普通函数、结构体、字符串、数组、循环和简单指针。功能分别放在多个 CPP，由 `main.cpp` 统一包含；项目没有自写头文件或测试目录。依赖为 SQLite 3.50.4、cJSON 1.7.19 和 libcurl ≥ 7.68，编译标准为 C++17。

| 技术 | 在项目中的作用 |
|---|---|
| C++17 | 普通函数、结构体、数组、指针与 CLI 主流程 |
| SQLite | 读取真实结构、执行只读 SQL、取得结果 |
| cJSON | 读取配置，生成 API 请求并解析回复 |
| libcurl | 在进程内发送 HTTPS 请求，接收模型返回 |

`agent.cpp` 控制查询与分析，`database.cpp` 集中 SQLite 调用，`llm.cpp` 与 `json.cpp` 完成 API 协议，`files.cpp` 读取配置和处理路径，`cli.cpp` 展示输出。模块和资源释放的完整说明见[技术文档](design-and-implementation.md)。

### 3.2 重点代码：查询主流程

以下节选来自当前源码。生成 SQL 的主流程省略了三处失败返回分支；CLI 片段展示查询结果先于分析输出的调用顺序。函数内部片段依赖所在函数的变量，完整实现可通过各节的源码链接阅读。

来源：[src/agent.cpp：run_query](../src/agent.cpp)。

```cpp
TextResult schema = read_schema(db);
// ... 省略结构读取失败的返回分支
Status setup = start_completion(source, system_prompt,
                                "问题：" + question + "\n数据库结构：\n" + schema.text);
// ... 省略上下文构建失败的返回分支
TextResult reply = request_text(source);
close_completion(source);
// ... 省略请求失败的返回分支
result = execute_reply(db, reply.text);
```

CLI 中的显示与分析顺序，来源：[src/cli.cpp：run_chat](../src/cli.cpp)：

```cpp
QueryRun result = run_query(&db, question, &source);
print_result(&result);
if (result.success && !cancelled) {
    cout << "\n正在分析…\n" << flush;
    result.analysis = analyze_result(question, &result.query, &source,
                                     config->database.max_preview_rows);
    print_analysis_result(&result);
}
free_query(&result.query);
```

**讲解重点：** `read_schema` 取得真实结构，`request_text` 调用模型，`execute_reply` 识别标记或执行 SQL。`run_query` 只负责查询，CLI 先打印 SQL 和数据，再单独调用 `analyze_result`；分析结束后释放查询数组。无效输入、SQL 失败或已取消时不请求分析。

### 3.3 重点代码：打包 API 请求与获取回复

来源：[src/llm.cpp：request_text](../src/llm.cpp)，JSON 打包和回复解析的完整实现位于 [src/json.cpp](../src/json.cpp)。

```cpp
TextResult request = model_request_json(source);
if (request.success && request.text.length() <= 524288) {
    TextResult response = curl_post(&source->api, request.text);
    if (response.success) {
        result = read_model_response(response.text);
    }
}
```

**讲解重点：** `model_request_json` 将模型名称、system/user 消息和请求选项打包成 JSON；`curl_post` 在进程内发送 HTTPS；`read_model_response` 取出回复文字。这段代码同时用于 SQL 生成和结果分析，请求 JSON 上限为 512 KiB，失败信息在函数出口统一处理。

### 3.4 重点代码：SQLite 逐行执行

来源：[src/database.cpp：execute_sql](../src/database.cpp)。片段位于 `while (ready)` 的读取循环内。

```cpp
int code = sqlite3_step(statement);
if (code == SQLITE_DONE) {
    result.success = true;
    break;
}
if (code != SQLITE_ROW) {
    break;
}
```

**讲解重点：** `sqlite3_step` 每次返回一行；遇到 `SQLITE_DONE`，说明结果已读完，可以标记成功；其他非行状态结束读取，交给统一失败处理。取得行后，后续代码用简单指针保存单元格，并检查行数和结果大小。只读授权、语句准备、超时和资源释放保留在完整函数中。

### 3.5 重点代码：独立分析上下文

来源：[src/agent.cpp：analyze_result](../src/agent.cpp)。以下为完整函数。

```cpp
TextResult analyze_result(string question, QueryResult *query, Completion *source, int row_limit) {
    string input = "问题：" + question + "\nSQL：\n" + query->sql + "\n";
    input += result_preview(query, row_limit);
    // 分析使用独立提示词，不沿用第一次请求的“只返回 SQL”要求。
    Status setup = start_completion(source, analysis_prompt, input);
    if (setup.success == false) {
        return failed_text("API");
    }
    TextResult result = request_text(source);
    close_completion(source);
    return result;
}
```

**讲解重点：** 输入由当前问题、已执行 SQL 和 `result_preview` 组成，分析使用独立的 `analysis_prompt`。返回的 `TextResult` 保存在 `QueryRun.analysis` 中，查询数据则在 `QueryRun.query` 中；这样分析请求失败也能继续展示已有 SQL 和结果。

### 3.6 重点代码：无效输入标记拦截

来源：[src/agent.cpp：execute_reply](../src/agent.cpp)。该判断位于 SQL 执行之前。

```cpp
if (sql.text == invalid_query_marker) {
    // 无效输入到这里就结束，避免执行占位 SQL。
    result.invalid_input = true;
    result.error = invalid_query_hint;
    return result;
}
```

**讲解重点：** 模型按提示词返回固定标记后，这里直接结束查询。CLI 根据 `invalid_input` 显示固定引导；该分支不会进入 `execute_sql`，也不会请求结果分析。有效查询内容中包含 help/exit 时，仍可按普通 SQL 路径处理。

### 3.7 文档评测记录方法

评测工具已移除，已有结论在第 4.4 节保留，新增记录采用人工核对。选择具体问题与 SQLite 文件，通过普通查询取得预测 SQL、数据及分析，在本地只读数据库客户端执行标准 SQL，核对列、值、重复次数和排序。

每次记录日期、源码版本、平台、模型、配置、问题、数据库、预测与标准 SQL、预期和实际结果、是否一致及分析是否忠实。截断、请求失败或无法比较也需要记录原因；标准 SQL 不发送给模型。具体方法见[技术文档](design-and-implementation.md#7-文档评测记录)。

## 4. 结果展示

### 4.1 学生人数查询

启动 `./build/text2sql` 后输入“查询学生总人数”，SQL、结果和分析分别展示。例如：

```text
SQL 语句
--------------------------------
SELECT COUNT(*) FROM "student";

查询结果
--------------------------------
COUNT(*)
2000
1 行
--------------------------------

正在分析…

分析结果
--------------------------------
student 表共有 2000 名学生。
--------------------------------
```

这是展示格式示例，模型生成的列别名或解释措辞可能不同；验证中实际执行结果为 2000。

### 4.2 多表关联与排名

输入“列出选课记录数量最多的5门课程，显示课程名称和选课记录数”，模型需要关联 `course` 与 `takes`、按课程分组并排序。已验证的一组输出为：

| 课程名称 | 选课记录数 |
|---|---:|
| Embedded Systems | 980 |
| Image Processing | 634 |
| The IBM 360 Architecture | 626 |
| Surfing | 613 |
| Visual BASIC | 610 |

第五名存在并列，Fractal Geometry 也有 610 条记录；原问题未指定并列次序，上表展示其中一种有效 Top-5 结果。这里统计的是选课记录数，不能直接称为去重后的学生人数。

### 4.3 无效输入和错误展示

单独输入 `help` 或 `exit` 时，已验证会显示固定引导提示，不执行 SQL，也不请求结果分析。输入 `/help` 可直接查看帮助，`/exit` 退出。

结果分析失败时，已执行的 SQL 和数据仍显示；SQL 执行失败时显示该 SQL 与统一错误，用户可以继续提问。每个问题保持简单的单次 SQL 流程。

### 4.4 功能验证与评价范围

以下保留 2026-11-02 已记录的历史功能验证，原记录对应 `5f8b04b` 阶段，覆盖真实查询、结果分析、无效输入和默认数据定位。移除评测工具不会将这些历史结果变为当前版本的新评测。

| 验证内容 | 实际结果 | 说明 |
|---|---|---|
| 默认数据库 | 11 张表，学生 2000、教师 50、课程 200 | college_2 的真实 SQLite 数据 |
| 真实 API 查询与分析 | 学生总数正确，多表 Top-5 与本地查询一致，分析成功 | Top-5 按并列结果允许的范围核对 |
| 无效输入 | 裸 help/exit 显示固定提示 | 未执行模型 SQL 或结果分析 |
| 分析错误处理 | 保留 SQL 和查询结果 | 独立分析失败分支已验证 |
| 历史两题样例评测 | 2/2 执行成功，2/2 结果一致 | 原工具运行结果，仅保留文档结论；工具已移除 |
| 复现与可移植启动 | 干净本地 clone 编译、默认启动和复制后启动通过 | 本机 macOS 验证 |

历史两题记录的具体内容如下，问题和标准 SQL 仍保存在 [sample-spider/dev.json](../data/sample-spider/dev.json)：

| 问题 | 数据库 | 标准结果 | 历史实际结果 | 历史结论 |
|---|---|---|---|---|
| 查询员工总人数，只返回员工总数 | company | 6 | 6 | 执行成功、结果一致 |
| 按部门名称升序查询每个部门的员工人数 | company | Engineering 2；Finance 2；Marketing 2 | Engineering 2；Finance 2；Marketing 2 | 执行成功、结果一致 |

对应标准 SQL：

```sql
SELECT COUNT(*) FROM employees;

SELECT d.name, COUNT(e.id)
FROM departments d
JOIN employees e ON e.department_id = d.id
GROUP BY d.name
ORDER BY d.name;
```

历史结论来自此前文档记录，仓库未保存当次模型预测 SQL 与原始结果文件；上面的 SQL 是数据集标准 SQL。这组 2/2 结果用于确认当时的样例流程，不能推出完整 Spider 准确率。

**2026-11-09 移除工具后的验证：**

本次验证对象是基于 `fc8dafd` 的精简改动；环境为 macOS、AppleClang 21，分别完成普通构建和 ASan/UBSan 构建。数据库、数据集及其来源清单与修改前字节一致，查询与分析提示词、API 实现、配置和三平台 CI 定义未改动。

| 检查内容 | 本次实际结果 | 验证方式与范围 |
|---|---|---|
| 干净本地 clone | 应用精简改动后配置、编译和默认启动成功 | 无本地密钥配置，默认连接 college_2 |
| ZIP 与安装逻辑移除 | 新构建目录没有展开的 Spider；项目安装声明已移除 | 构建只复制现成数据，仓库 ZIP 保留 |
| 启动与文件路径 | 16 项检查通过 | 包括帮助、旧评测命令拒绝、配置相对路径、复制运行和多配置目录布局 |
| 查询与分析保留 | 31 项检查通过，无 sanitizer 报错 | 真实 SQLite 执行；模型回复使用本地 Mock |
| 员工总数 | 6 | 本地执行上面的样例标准 SQL |
| 各部门员工人数 | Engineering 2；Finance 2；Marketing 2 | 本地执行上面的样例标准 SQL |
| 大学库查询 | 学生 2000；Top-5 首项 Embedded Systems，980 条选课记录 | 本地计数与 JOIN 聚合核对 |
| 分阶段显示与失败处理 | 查询先显示；分析失败保留数据；取消路径退出码为 130 | Mock 分析请求，核对请求前的数据输出与刷新 |

本次没有调用真实模型，也没有在 Windows/Linux 实机运行；本地 SQL 核对与 Mock 检查属于功能回归，不能作为新的模型准确率。检查程序和临时输出保存在源码目录外，项目继续不保留评测工具。

当前没有完整 dev 集的本版本评测结果。只在一个数据库实例上比较结果，也不能证明 SQL 在其他数据实例上的语义等价。本地结果一致率与 Spider 官方 Test Suite Accuracy 使用不同的评价方法。

Windows 与 Linux 的构建配置、Windows 中文参数适配和三平台 CI 定义保留；本报告汇总的实际运行记录来自 macOS。

## 5. 总结与展望

### 5.1 项目总结

项目完成了数据库结构填充、模型 SQL 生成、真实 SQLite 执行、独立结果分析和 CLI 输出。固定流程与集中错误处理减少了控制分支，基础 C++ 写法与分模块组织便于阅读；college_2 直接纳入仓库，使 clone 后的构建和体验更容易复现。

历史验证覆盖学生人数、多表排名、无效输入拦截、分析失败保留结果、两题样例以及默认启动和复制后的数据定位。项目现在以文档保存评价记录；查询效果可以通过 SQL 和真实结果直接核对，评测结论应结合版本和数据范围理解。

### 5.2 局限与后续工作

当前 SQL 语义、输入意图判断和结果分析质量仍受模型影响；连续提问需要补全上下文，过大的结果只展示有限数据。后续重点是扩大评价覆盖，并检查模型解释与执行结果的一致性：

- 按需手动解压 Spider，选取更多数据库的问题，记录源码版本、模型、配置、SQL 和实际结果，在本文形成可复查的评价记录。
- 分析字段匹配、JOIN、聚合和 Top-N 错误，区分执行成功、结果一致与语义正确，明确去重和并列排名等问题的口径。
- 核对分析文字与已执行 SQL、真实结果预览的对应关系，检查部分结果的提示是否清楚，避免将有限预览推断成全部数据。

### 5.3 现场汇报与演示建议

现场也按这五部分展开，建议总时长 3～5 分钟。实现方法优先展示 3.2 主流程和 3.5 独立分析，再按需要补充 3.4 SQL 执行、3.6 标记拦截或 3.7 文档记录方法。

| 部分 | 展示与讲述重点 |
|---|---|
| 项目背景与意义 | 自然语言查询的需求、Spider 的跨库特点、college_2 的数据规模 |
| 设计思路 | 固定流程、两次 API 的职责、只读执行与结果预览边界 |
| 实现方法 | 用主流程和分析上下文代码解释如何完成查询与分析 |
| 结果展示 | 学生人数、多表 Top-5、无效输入提示、功能验证及两题样例的评价范围 |
| 总结与展望 | 已完成的功能、当前限制和扩大手动评测的后续工作 |

演示前按 README 构建并配置 API Key。启动后确认连接 college_2，先查询学生总人数，再查询选课记录 Top-5，最后输入裸 `help` 展示固定提示。需要核对其他问题时，使用普通查询入口，并把预测 SQL、实际数据和核对结论补充到本文。

### 5.4 参考资料

1. Yu et al. *Spider: A Large-Scale Human-Labeled Dataset for Complex and Cross-Domain Semantic Parsing and Text-to-SQL Task*. EMNLP 2018，[Spider 官方页面](https://yale-lily.github.io/spider)。
2. [SQLite 官方文档](https://www.sqlite.org/docs.html)。
3. [cJSON 1.7.19](https://github.com/DaveGamble/cJSON/tree/v1.7.19)。
4. [libcurl 官方文档](https://curl.se/libcurl/)。
