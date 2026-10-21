# Agentic Text-to-SQL 项目汇报文稿

## 0. 文稿定位

本文是一份面向项目汇报、课程展示、技术答辩和个人作品集的完整中文材料。不了解 C++、数据库或大模型的读者，可以通过本文理解项目解决了什么问题、如何工作、为什么这样设计、目前验证到了什么程度，以及下一步如何完善。文末给出可直接用于制作 PPT 的页面结构和答辩问答。

项目名称：**Agentic Text-to-SQL**

项目形态：C++20 命令行工具

代码仓库：https://github.com/Lx6uo/agentic-text2sql-cpp

核心技术：C++20、SQLite、DeepSeek Flash、Tool Calling、libcurl、CMake、Spider 1.0

---

## 1. 项目摘要

Agentic Text-to-SQL 是一个轻量、免训练、跨数据库的自然语言查询工具。用户不需要掌握 SQL，只需连接一个已有 SQLite 数据库，然后输入“哪个部门的平均工资最高”“一共有多少名歌手”等自然语言问题，程序就会让大模型自主查看数据库结构、生成只读 SQL、执行查询，并在 SQL 出错时根据真实错误有限修正。

项目的重点不是做一个聊天界面，而是构建一条安全、可验证的自然语言数据库查询链路：大模型负责理解问题和规划工具，C++ 程序负责数据库权限、安全边界、执行限制、结果展示和评测。即使模型生成了写入、删除或多条 SQL，SQLite 执行层也会拒绝，而不是仅依靠提示词要求模型“不要这样做”。

项目不训练模型、不部署本地大模型、不依赖数据库服务器，也没有引入 LangChain、Web 服务、GUI 或多 Agent 框架。离线 Demo 不需要 API Key 和网络；真实查询使用 DeepSeek `deepseek-flash`；实验部分支持 Spider 1.0 的 Baseline 与 Agentic 对照。

一句话概括：

> 让用户用自然语言安全查询任意 SQLite 数据库，并让大模型通过工具调用完成 Schema 探索、SQL 生成、真实执行和错误修正。

---

## 2. 项目背景：为什么需要 Text-to-SQL

关系型数据库中的数据通常通过 SQL 查询。SQL 很强大，但存在三个现实门槛：

1. 非技术用户不知道表名、字段名、连接条件和 SQL 语法。
2. 即使会 SQL，也需要先阅读数据库结构，跨数据库迁移时要重新熟悉 Schema。
3. 让大模型直接生成 SQL 虽然方便，但模型可能猜错字段、生成多条语句，甚至产生写入和删除操作。

传统 Text-to-SQL 常见做法是一次性把完整 Schema 发给模型，让模型输出一条 SQL。这种方式结构简单，但数据库稍复杂时会出现上下文冗余、字段幻觉、错误无法闭环等问题。另一类方案会训练或微调专用模型，工程成本和环境要求较高，不适合作为轻量 C++ 个人项目。

本项目选择 Agentic 路线：不训练模型，而是给模型少量受控工具，让它根据问题自主决定是否查看表、查看哪些字段、何时执行 SQL，以及是否根据错误进行修正。这样既保留大模型的语言理解能力，又把数据库执行权掌握在确定性的 C++ 代码中。

---

## 3. 项目目标与边界

### 3.1 核心目标

- 使用 C++20 开发可独立构建的命令行程序。
- 支持任意已有 SQLite 数据库，不写死 Demo 字段映射。
- 通过 DeepSeek Tool Calling 动态探索实际 Schema。
- 只允许只读 SQL，在 SQLite 层强制安全限制。
- SQL 出错后，把真实错误反馈给模型并允许有限修正。
- 同时支持交互式 REPL、单次命令、verbose 和 JSON 输出。
- 提供无需网络和密钥的离线 Demo。
- 使用 Spider 1.0 验证跨数据库流程，比较 Baseline 和 Agentic 模式。
- 支持 macOS、Linux 和原生 Windows/MSVC。
- 有测试、有文档、有分阶段 Git 历史，能够作为完整个人项目展示。

### 3.2 明确不做的内容

- 不训练、微调或部署本地大模型。
- 不实现数据库写入。
- 不做 GUI、TUI、Web 服务或全屏终端界面。
- 不做多 Agent、向量数据库、长期记忆或插件框架。
- 不引入 Python、Node.js、Docker 作为主程序运行依赖。
- 不把本地结果一致率冒充 Spider 官方 Test Suite Accuracy。

这些边界体现了项目的 KISS 原则：把工程复杂度集中在真正影响功能、安全和可验证性的部分。

---

## 4. 面向的用户和使用场景

### 4.1 目标用户

- 想快速浏览 SQLite 数据的普通用户。
- 需要临时分析本地数据库的开发者或数据人员。
- 正在学习 C++、SQLite 和大模型工具调用的学生。
- 希望研究 Agentic Text-to-SQL 工作流的实验者。

### 4.2 典型场景

- 对公司人员数据库提问：“平均工资最高的三个部门是什么？”
- 对音乐数据库提问：“共有多少名歌手？”
- 对陌生数据库先问：“有哪些表？”再查看字段结构。
- 通过 JSON 模式将查询结果交给其他程序处理。
- 在 Spider 数据集上批量比较一次生成和动态工具调用的差异。

---

## 5. 用户如何使用项目

### 5.1 离线体验

```bash
cmake -S . -B build
cmake --build build
./build/text2sql demo
./build/text2sql demo --repair --verbose
```

离线 Demo 明确显示 `Demo Mode (Mock LLM)`。Mock 只模拟模型发出的工具消息，Schema 读取、安全判断、SQL 错误和结果都由真实 SQLite 执行，因此它既不冒充真实模型，也能在没有 API Key 时验证完整的本地执行链路。

### 5.2 交互式查询

```bash
./build/text2sql --db data/company.sqlite --verbose
```

进入 REPL 后，可以使用：

| 命令 | 功能 |
|---|---|
| `/help` | 显示帮助 |
| `/open <path>` | 打开另一个 SQLite 数据库 |
| `/tables` | 本地列出表和视图，不调用模型 |
| `/schema` | 本地显示字段、主键和外键 |
| `/model` | 显示模型和密钥配置状态 |
| `/verbose on/off` | 开关 Agent 执行轨迹 |
| `/exit` | 退出程序 |

普通文本被当作自然语言问题。例如：

```text
> 查询平均工资最高的三个部门
[1] list_tables: Success
[2] describe_tables: Success
[3] execute_sql: Success

SQL:
SELECT ...

Results:
...
```

### 5.3 单次调用和机器集成

```bash
./build/text2sql query \
  --db data/company.sqlite \
  --question "Count employees" \
  --json
```

JSON 模式保证 stdout 只有有效 JSON，错误和 verbose 日志进入 stderr，便于后续被脚本、桌面程序或其他服务调用。

### 5.4 可直接复制运行的输入输出实录

以下命令均从项目根目录执行。示例一和示例五完全离线，不需要 API Key；示例二至示例四使用 `data/company.sqlite`。如果该文件不存在，可先执行：

```bash
sqlite3 data/company.sqlite < examples/demo.sql
```

真实模型查询还要求已经复制并填写 `config.json`。离线输出中的耗时会因机器而变化；真实 DeepSeek 生成的 SQL 格式、回答措辞、耗时和 token 可能随每次运行略有变化，但查询语义和数据库结果应保持一致。

#### 示例一：离线体验 SQL 出错并自动修正

输入：

```bash
./build/text2sql demo --repair --verbose
```

本次实际输出：

```text
[1] list_tables: Success
[2] describe_tables: Success
[3] execute_sql: Failed — no such column: department_name
[4] execute_sql: Success
Demo Mode (Mock LLM)

SQL:
SELECT d.name AS department, ROUND(AVG(e.salary), 2) AS average_salary FROM employees e JOIN departments d ON e.department_id = d.id GROUP BY d.id, d.name ORDER BY average_salary DESC LIMIT 3

Results:
department   average_salary
Engineering  115000.0
Finance      95000.0
Marketing    82500.0
3 row(s)

已按平均工资降序查询三个部门。结果来自真实 SQLite 执行。
Agent steps: 5, SQL attempts: 2, repairs: 1, elapsed: 0.957667 ms
```

这段输出可以直接用于 PPT 演示 Agent 的闭环：前两步发现表和字段，第三步收到真实 SQLite 字段错误，第四步修正后执行成功。

#### 示例二：不调用模型，直接查看数据库结构

输入：

```bash
./build/text2sql schema --db data/company.sqlite
```

实际输出：

```text
departments (table)
  id INTEGER PRIMARY KEY
  name TEXT
employees (table)
  id INTEGER PRIMARY KEY
  name TEXT
  department_id INTEGER
  salary REAL
  note TEXT
  FK "department_id" -> "departments"."id"
```

这个命令只在本地读取 SQLite Schema，不使用 API Key，也不产生模型费用。

#### 示例三：真实 DeepSeek 回答“数据库有哪些表”

输入：

```bash
./build/text2sql query \
  --db data/company.sqlite \
  --question "这个数据库有哪些表？" \
  --verbose
```

本次真实 API 输出：

```text
[1] list_tables: Success

这个数据库包含 2 张表：

1. **departments**（部门表）
2. **employees**（员工表）

需要我查看这些表的字段结构或数据内容吗？
Agent steps: 2, SQL attempts: 0, repairs: 0, elapsed: 1968.932292 ms
```

这个问题只需要 Schema 信息，因此 Agent 调用 `list_tables` 后直接完成，没有为了形式而执行无意义 SQL。

#### 示例四：真实 DeepSeek 完成 JOIN、聚合和排序

输入：

```bash
./build/text2sql query \
  --db data/company.sqlite \
  --question "查询平均工资最高的三个部门，返回部门名称、平均工资和员工人数" \
  --verbose
```

本次真实 API 输出：

```text
[1] list_tables: Success
[2] describe_tables: Success
[3] execute_sql: Success

SQL:
SELECT d.name AS 部门名称, AVG(e.salary) AS 平均工资, COUNT(e.id) AS 员工人数 FROM employees e JOIN departments d ON e.department_id = d.id WHERE e.salary IS NOT NULL GROUP BY d.id, d.name ORDER BY 平均工资 DESC LIMIT 3

Results:
部门名称     平均工资  员工人数
Engineering  115000.0  2
Finance      95000.0   2
Marketing    82500.0   2
3 row(s)

平均工资最高的三个部门如下（按平均工资降序）：

| 部门名称 | 平均工资 | 员工人数 |
|---|---|---|
| Engineering | 115,000.0 | 2 |
| Finance | 95,000.0 | 2 |
| Marketing | 82,500.0 | 2 |

说明：查询将 `employees` 与 `departments` 按部门 ID 关联，按部门分组计算工资平均值（排除工资为空的记录）和员工人数，取平均工资最高的 3 个部门。
Agent steps: 4, SQL attempts: 1, repairs: 0, elapsed: 3647.364833 ms
```

#### 示例五：输出机器可读 JSON

输入：

```bash
./build/text2sql demo --repair --json
```

实际 stdout 是一个完整 JSON 文档：

```json
{"agent_steps":5,"answer":"已按平均工资降序查询三个部门。结果来自真实 SQLite 执行。","elapsed_ms":0.973125,"error":null,"mode":"Demo Mode (Mock LLM)","query":{"cancelled":false,"columns":["department","average_salary"],"error":null,"execution_ms":0.13175,"query_id":2,"rows":[["Engineering",115000.0],["Finance",95000.0],["Marketing",82500.0]],"sql":"SELECT d.name AS department, ROUND(AVG(e.salary), 2) AS average_salary FROM employees e JOIN departments d ON e.department_id = d.id GROUP BY d.id, d.name ORDER BY average_salary DESC LIMIT 3","success":true,"timed_out":false,"truncated":false},"repairs":1,"sql_attempts":2,"success":true,"termination":"completed","tool_calls":4,"trace":[{"step":1,"success":true,"tool":"list_tables"},{"step":2,"success":true,"tool":"describe_tables"},{"error":"no such column: department_name","step":3,"success":false,"tool":"execute_sql"},{"step":4,"success":true,"tool":"execute_sql"}],"usage":{}}
```

JSON 中同时保留最终答案、SQL、类型化结果、错误修正轨迹、停止原因和统计信息，可以直接被其他程序解析。

---

## 6. 系统架构

整体采用“单 Agent 控制器 + 单模型 + 三个确定性工具”的紧凑结构：

```text
自然语言问题
      │
      ▼
CLI / REPL
      │
      ▼
Agent Controller ───────────────┐
      │                         │
      ▼                         │
DeepSeek Flash                  │ 多轮消息与 tool_call_id
      │                         │
      ▼                         │
工具选择                        │
  ├─ list_tables                │
  ├─ describe_tables            │
  └─ execute_sql                │
          │                     │
          ▼                     │
SQLite 安全层 → 结果或错误 ─────┘
      │
      ▼
SQL、表格、回答、统计或 JSON
```

各模块职责如下：

| 模块 | 主要职责 |
|---|---|
| `main` | 参数解析、REPL、命令、文本表格、JSON 输出 |
| `config` | 配置文件、环境变量优先级和范围校验 |
| `database` | SQLite 连接、Schema、authorizer、安全执行、限制 |
| `llm` | libcurl HTTPS 请求、DeepSeek 消息和 Tool Calling 解析 |
| `agent` | 多轮消息、工具分发、错误修正、停止条件和统计 |
| `benchmark` | Spider 加载、两种模式、gold 执行、结果比较和导出 |

项目没有为简单问题引入大型抽象框架。真实模型和 Mock 都通过同一种 Completion 接口进入 Agent，因此测试路径与产品路径保持一致。

---

## 7. Agent 的三个工具

### 7.1 `list_tables`

读取数据库真实表和视图。它适合回答“数据库有哪些表”，也帮助模型先缩小搜索范围。结果来自 `sqlite_schema`，不会使用预置数据库描述。

### 7.2 `describe_tables`

输入模型选择的表名，返回：

- 字段名称和类型。
- 是否为主键及复合主键顺序。
- 外键来源表、字段和目标字段。
- 表或视图类型。

模型不必每次接收完整数据库 Schema，而可以只查看与当前问题有关的表。

### 7.3 `execute_sql`

输入一条 SQL，C++ 程序完成安全编译和只读执行。结果包含：

- `success`、`sql`、`error`。
- `columns` 和类型化 `rows`。
- `execution_ms`、`truncated`、`query_id`。
- `timed_out` 和 `cancelled`。

默认本地最多展示 100 行，回传给模型的预览最多 10 行，并有额外字节上限，避免大量数据库内容进入模型上下文。

---

## 8. Agentic 工作流与错误修正

每次问题启动一个有限状态循环：

1. C++ 发送系统规则和用户问题。
2. 模型决定直接回答还是调用工具。
3. C++ 验证工具名和参数。
4. 执行工具，把结果按原 `tool_call_id` 返回模型。
5. 模型根据 Schema、结果或 SQL 错误继续决策。
6. 最后一次答案查询成功且模型给出最终回复时结束。

默认限制为：最多 8 轮模型请求、3 次 SQL 尝试、2 次错误修正。还包含 API 错误、数据库错误、上下文大小、用户取消等停止条件，因此不会无限循环。

离线修正 Demo 的第一条 SQL 故意使用不存在的 `department_name`：

```sql
SELECT department_name, AVG(salary)
FROM employees
GROUP BY department_name;
```

SQLite 返回 `no such column: department_name`。随后 Mock 模型根据真实 Schema 改为连接 `employees` 与 `departments` 的查询，第二次执行成功。这个过程验证的不是预置结果，而是“真实错误进入上下文，再次执行真实 SQL”的闭环。

对于“数据库有哪些表”这类只需 Schema 工具的问题，Agent 可以在不执行 SQL 的情况下正常完成。项目曾在真实体验中发现这类回答被误标为 `unable_to_answer`，现已增加专门回归测试并修复。这体现了项目通过实际使用持续调试，而不只是让单元测试表面通过。

---

## 9. SQLite 安全设计

安全边界建立在 SQLite 执行层，而不是只依赖字符串前缀或模型自律。

### 9.1 多层只读防护

- 文件数据库使用 `SQLITE_OPEN_READONLY` 打开。
- 启用 `query_only` 和 defensive 模式。
- 关闭 trusted schema 和扩展加载。
- 使用 `sqlite3_prepare_v3` 编译 SQL。
- 使用 `sqlite3_stmt_readonly` 检查语句属性。
- 解析 SQL 尾部，拒绝第二条语句。
- 使用 `sqlite3_set_authorizer` 逐操作授权。

因此 INSERT、UPDATE、DELETE、CREATE、DROP、ALTER、ATTACH、DETACH、事务控制、未授权 PRAGMA 和未知函数都会被拒绝。合法的 SELECT、JOIN、CTE、聚合、窗口函数和受控 JSON 函数仍可使用。

### 9.2 资源限制

- SQL 默认最多 16 KB。
- 单条查询默认最多执行 5 秒。
- 单值长度、字段数、表达式深度、复合查询数和虚拟机操作数都有限制。
- 本地结果默认最多 100 行和约 1 MB。
- Schema 读取也有时间、条数和字节限制。
- progress handler 定期检查超时和 Ctrl+C。

这些措施降低了复杂递归查询、超大结果和恶意 SQL 消耗过多资源的风险。

### 9.3 隐私与提示注入

默认发送给模型的是问题、所选 Schema 和有限工具预览，而不是完整数据库文件。系统提示明确把数据库内容视为不可信数据，不能覆盖安全规则。

提示词不能从理论上消除所有提示注入，但模型无论如何都无法绕过 C++ 和 SQLite 的只读授权。因此项目把“模型是否听话”和“数据库是否安全”分成两个层次处理。

---

## 10. DeepSeek 与 libcurl 网络层

真实模式使用 DeepSeek Chat Completions：

- Base URL：`https://api.deepseek.com`
- 模型：`deepseek-flash`
- 非流式：`stream=false`
- 非思考模式：`thinking.type=disabled`
- 工具选择：`tool_choice=auto`

项目通过 libcurl easy API 在进程内发送 HTTPS，不调用 shell，也不启动外部 curl 子进程。API Key 只放入内存中的认证头，不进入命令行和临时文件。网络层强制 HTTPS、验证证书和主机名、禁止重定向，并限制连接时间、总时间和解压后的响应大小。libcurl progress callback 可以响应用户取消。

API Key 可以放在本地 `config.json`，也可以通过 `DEEPSEEK_API_KEY` 环境变量提供；环境变量优先。示例配置中的 Key 为空，真实配置被 `.gitignore` 排除。

---

## 11. 离线 Demo 数据和实际结果

Demo 包含两张表：

- `departments(id, name)`
- `employees(id, name, department_id, salary, note)`

数据包含 Engineering、Finance、Marketing 三个部门和 6 名员工，也包含 NULL、中文姓名与外键关系。

“平均工资最高的三个部门”实际结果为：

| 部门 | 平均工资 | 员工数 |
|---|---:|---:|
| Engineering | 115000 | 2 |
| Finance | 95000 | 2 |
| Marketing | 82500 | 2 |

真实 DeepSeek 查询还验证了：

- 列出备注为空的员工，正确处理 `IS NULL`、空字符串和 JOIN。
- 统计部门人数，正确使用 LEFT JOIN、GROUP BY 和多字段排序。
- 仅调用 `list_tables` 回答 Schema 问题，不执行不必要的 SQL。

---

## 12. Spider 1.0 评价设计

Spider 是跨领域 Text-to-SQL 公开数据集，包含自然语言问题、标准 SQL 和多个 SQLite 数据库。本项目不使用 Spider 训练模型，只用它验证方法能否面对不同 Schema。

当前本地官方数据包含：

- 166 个 SQLite 数据库。
- dev 1034 个问题，覆盖 20 个数据库。
- train_spider 7000 个问题。

项目支持两种实验模式：

| 模式 | 输入和行为 |
|---|---|
| Baseline | 一次发送完整 Schema，模型只生成一次 SQL，不修正 |
| Agentic | 模型动态调用 Schema 工具，执行 SQL，并可利用错误修正 |

标准 SQL 只在模型完成后由独立只读连接执行，不会提供给模型。程序比较预测 SQL 与标准 SQL 的执行结果，并记录：

- SQL 执行成功率。
- 本地结果一致率。
- 可比较题数。
- Agent 平均轮次。
- SQL 平均尝试次数。
- 平均耗时。
- API 返回的 token 使用量。

结果比较保留重复行；gold SQL 存在外层 `ORDER BY` 时比较顺序，否则忽略行顺序；数字使用相对容差。截断结果不会被判为一致。

程序导出 JSONL 明细和 `SQL<TAB>db_id` 预测文件，可进一步交给 Spider 官方评价工具。必须强调：本地执行结果一致率不是官方 Test Suite Accuracy，某一数据库实例上结果一致也不能证明 SQL 在所有数据实例上语义等价。

---

## 13. 当前实验结果应如何解读

官方 Spider dev 的初始小规模连通性验证选择了前 3 题：

- Baseline：3/3 SQL 执行成功，3/3 本地结果一致。
- Agentic：3/3 SQL 执行成功，3/3 本地结果一致。
- Agentic 平均 4 轮、1 次 SQL 尝试。
- Baseline 共使用 3038 tokens，Agentic 共使用 11041 tokens。

切换 libcurl 后，又对官方 dev 第 1 题进行了 Baseline/Agentic 回归，二者均执行成功并与 gold 一致。

这些结果证明真实 API、工具调用、官方 SQLite、gold 执行和导出链路已经打通，但不能把 3/3 写成模型准确率：三题都来自 `concert_singer`，难度较低且样本太小。严谨的下一步应当是按数据库和难度分层抽取 30 至 50 题，再决定是否投入约数百万 tokens 跑完整 dev 1034 题。

---

## 14. 跨平台与工程化

### 14.1 Windows 支持

项目最初的 HTTP 层使用 POSIX 子进程启动系统 curl，因此只能在 macOS/Linux 使用。后来改为 libcurl，并同时完成：

- Windows UTF-16 命令行通过 `CommandLineToArgvW` 读取并转换为 UTF-8。
- Windows 控制台输入输出切换为 UTF-8。
- 中文配置、数据库、数据集和结果路径使用跨平台 `std::filesystem::path`。
- MSVC 使用 `/W4`、`/permissive-`、`/utf-8`。
- Windows 通过 vcpkg 使用静态 libcurl，避免漏带 DLL。
- Benchmark 输出由 POSIX 文件描述符改为 `std::ofstream`。

### 14.2 构建与依赖

- C++20 与 CMake 3.20 以上。
- SQLite 3.50.4 amalgamation 固定在仓库中。
- nlohmann/json 3.12.0 单头文件固定在仓库中。
- libcurl 7.68 以上由系统或包管理器提供。
- 第三方版本、来源、许可证和 SHA-256 均有记录。

### 14.3 持续集成

GitHub Actions 定义了 Ubuntu、macOS、Windows 三个 job。每个平台都会执行依赖安装、CMake 配置、编译、CTest 和离线修正 Demo。CI 不读取真实 API Key，避免在公共流水线中产生费用或泄漏密钥。

首次远端运行真实暴露了 Win32 头文件顺序、MSVC 静态运行库和 JSON 初始化歧义，项目据此连续修复。最终 Ubuntu、macOS、Windows/MSVC 三个平台都完成了配置、编译、5 项 CTest 和离线修正 Demo。这一过程说明跨平台支持不是只写条件编译，而是经过远端原生 runner 验证。

---

## 15. 测试与验证

当前本地验证包括：

- Release CTest：5/5 通过。
- AddressSanitizer/UndefinedBehaviorSanitizer：5/5 通过。
- 72 项核心检查。
- 21 项跨平台 Benchmark 检查。
- GitHub Actions 的 Ubuntu、macOS、Windows/MSVC 全部通过。
- 真实 DeepSeek Tool Calling。
- 官方 Spider SQLite 小样本。
- 安装后、脱离源码目录运行 Demo。
- 第三方源码 SHA-256 校验。

核心检查覆盖数据库打开、Schema、外键、JOIN、聚合、NULL、CTE、窗口函数、中文标识符、写操作拒绝、多语句拒绝、未知函数、超时、截断、取消、Agent 修正、错误上限、非法工具参数、异常 API 信封和结果比较。

便携测试覆盖中文路径、真实临时 SQLite、Baseline/Agentic、JSONL 和预测 SQL 导出、已有文件保护，以及数据库测试前后字节完全一致。

Git 历史按项目初始化、数据库、模型和 Agent、CLI、Benchmark、测试文档、Windows/libcurl、交互 Bug 修复等阶段组织。公开仓库不包含 API Key、用户数据库、完整 Spider 数据、评测结果和构建产物。

---

## 16. 项目价值与亮点

### 16.1 功能不是代码片段，而是完整闭环

从自然语言输入到 Schema 探索、SQL 生成、安全执行、错误修正、结果展示和实验评价都能实际运行。

### 16.2 安全控制放在确定性执行层

模型负责建议，C++ 与 SQLite 决定是否允许执行。即使模型犯错，写数据库和系统命令也不在它的能力范围内。

### 16.3 Mock 与真实模型路径一致

离线 Demo 只替代模型输出，不替代 SQLite 执行。因此没有 API Key 时仍能验证核心工程逻辑。

### 16.4 支持跨数据库研究

没有为 Demo 表写固定字段映射，可以读取 Spider 等不同数据库的真实结构，并提供 Baseline/Agentic 对照和可追溯结果。

### 16.5 轻量且适合学习

没有大型 AI 框架，核心模块职责清楚，便于学习 C++ RAII、SQLite C API、HTTP、JSON、工具调用、命令行程序和工程测试。

### 16.6 通过真实使用发现问题

项目不是只依靠预先设计的测试。实际 REPL 体验发现了 Schema-only 回答状态错误和多行显示问题，随后补充回归测试、修复并重新验证，体现了真实的软件迭代过程。

---

## 17. 当前限制

1. **REPL 暂无多轮会话上下文。** 每个自然语言问题是独立任务，因此只输入“需要”“继续”等内容无法引用上一轮。这样做可避免跨数据库污染和 token 无限增长，但对话体验需要完善。
2. **Spider 实测规模仍小。** 当前只能证明流程打通，不能报告完整数据集准确率。
3. **未集成官方 Test Suite Accuracy。** 本地结果一致率有明确局限。
4. **Benchmark 暂无断点续跑。** API 或网络中断后会保存部分结果，但不能直接从已有 JSONL 继续。
5. **API 不自动重试。** 当前遇到 429 或 5xx 会停止，避免隐式重复付费，但长实验需要有限退避。
6. **模型执行成功不等于语义一定正确。** SQL 可以运行，仍可能误解自然语言问题。
7. **SQLite 防护不是操作系统沙箱。** 极端恶意或损坏数据库仍受进程内 SQLite 边界约束。
8. **终端排版是轻量实现。** 中文和长文本可用，但不追求完整 Unicode 表格渲染。

---

## 18. 下一阶段路线图

### 第一优先级：完善真实使用体验

- 为 REPL 增加最近若干轮短期上下文。
- `/open` 切换数据库时自动清空历史。
- 增加 `/clear` 和 `/context on|off`。
- 只保留问题和最终摘要，不重复发送大段工具结果。

### 第二优先级：让 Benchmark 可长期运行

- 增加 `--resume` 断点续跑。
- 支持按题号、数据库和难度筛选。
- 针对 429/5xx 增加有限次数、带抖动的退避重试。
- 保存 Git commit、运行配置和数据版本 manifest。

### 第三优先级：形成可信实验结论

- 先运行 30 至 50 题跨数据库、分难度评测。
- 分析 Agentic 相比 Baseline 在正确率、步骤、耗时和 token 上的收益与代价。
- 接入 Spider 官方 Test Suite Accuracy。
- 在预算允许时运行完整 dev 1034 题。

### 第四优先级：发布体验

- 生成带版本号的 macOS、Linux、Windows Release。
- 补充安装包或更简洁的依赖引导。
- 为公开版本增加演示 GIF、架构图和实验图表。

---

## 19. 项目结论

Agentic Text-to-SQL 证明了一个小型 C++ 项目也可以完整承载大模型工具调用：模型不直接拥有数据库权限，而是在受控工具中完成探索和决策；程序不训练模型，却能通过真实 Schema、执行反馈和有限修正适配不同数据库。

项目目前已经达到“可构建、可运行、可演示、可测试、可跨平台、可做小规模实验”的状态。最重要的成果不是让模型输出一条看起来合理的 SQL，而是建立了一套可追踪、可停止、有安全边界、能用公开数据验证的查询系统。下一阶段的重点将从核心功能开发转向多轮体验、规模化评测和实验复现。

---

## 20. PPT 制作建议：16 页完整结构

以下结构可以直接据本文制作一套约 12 至 18 分钟的项目汇报 PPT。

### 第 1 页：封面

标题：Agentic Text-to-SQL

副标题：基于 C++20、SQLite 与 DeepSeek Tool Calling 的安全自然语言数据库查询工具

补充：作者、日期、GitHub 地址。

建议视觉：终端中输入自然语言并输出 SQL/表格的截图。

### 第 2 页：问题背景

- SQL 对非技术用户有使用门槛。
- 陌生数据库需要先理解表和字段。
- 大模型直接生成 SQL 存在字段幻觉和写入风险。
- 训练专用模型成本过高。

建议视觉：自然语言、SQL、数据库三者之间的障碍图。

### 第 3 页：项目目标

- 自然语言查询任意 SQLite。
- 无训练、无本地模型、无数据库服务器。
- 动态 Schema 探索和错误修正。
- SQLite 层强制只读。
- CLI、Demo、Spider 评价、跨平台。

### 第 4 页：为什么选择 Agentic

对比：

| 一次生成 | Agentic |
|---|---|
| 一次发送完整 Schema | 按需探索相关表 |
| SQL 错误后结束 | 错误进入下一轮修正 |
| 固定单步 | 动态选择工具 |

强调：不做多 Agent，一个控制器和三个工具即可。

### 第 5 页：整体架构

使用第 6 节架构图，突出：CLI、Agent、DeepSeek、三个工具、SQLite 安全层、输出。

### 第 6 页：三个核心工具

- `list_tables`
- `describe_tables`
- `execute_sql`

说明每个工具的输入、输出和用途。

### 第 7 页：Agent 执行过程

用时间线展示：

```text
问题 → 查看表 → 查看字段 → 生成 SQL → 执行 → 错误反馈 → 修正 → 完成
```

列出默认 8 轮、3 次 SQL、2 次修正等停止限制。

### 第 8 页：SQL 安全设计

建议使用分层盾牌图：

```text
只读打开
  ↓
query_only / defensive
  ↓
prepare + stmt_readonly
  ↓
authorizer
  ↓
超时、行数、大小限制
```

强调不是字符串检查，也不是只依靠提示词。

### 第 9 页：CLI 与离线 Demo

- REPL 命令。
- 单次 query。
- verbose 和 JSON。
- Mock 只模拟模型，SQLite 仍真实执行。

建议放正常 Demo 和错误修正 Demo 的终端截图。

### 第 10 页：实际案例

问题：“查询平均工资最高的三个部门。”

展示生成 SQL 和 Engineering、Finance、Marketing 的结果表。

### 第 11 页：Spider 评价方法

- 官方问题、数据库和 gold SQL。
- gold 不提供给模型。
- Baseline 与 Agentic。
- 执行成功率、结果一致率、步骤、耗时、token。
- 本地指标不等于官方 Test Suite Accuracy。

### 第 12 页：当前实验结果

- 官方 dev 小样本 3 题。
- 两种模式均 3/3 执行和结果一致。
- Agentic 平均 4 轮。
- 样本都来自同一数据库，因此不做准确率外推。

建议使用柱状图展示步骤、耗时、tokens，并在图旁标记“小样本链路验证”。

### 第 13 页：工程质量与跨平台

- C++20 + CMake。
- SQLite/JSON 固定依赖，libcurl 网络层。
- Windows UTF-8/中文路径。
- Release 与 Sanitizer 5/5。
- 72 项核心检查、21 项 Benchmark 检查。
- 三平台 GitHub Actions。

### 第 14 页：项目亮点

- 确定性安全层与非确定性模型解耦。
- 动态 Schema，无固定字段映射。
- Mock 与真实执行路径一致。
- 可用 Spider 做对照实验。
- 轻量、可读、适合学习和长期维护。

### 第 15 页：限制与路线图

当前限制：多轮上下文、评测规模、断点续跑、官方指标。

下一步：短期上下文、分层 50 题、resume/retry、官方 Test Suite、Release。

### 第 16 页：总结

推荐结束语：

> 本项目不是让大模型“猜一条 SQL”，而是让模型在安全、有限、可观测的工具环境中完成数据库查询任务。

放置 GitHub 地址和 Demo 命令。

---

## 21. 答辩常见问题与参考回答

### Q1：为什么使用 C++，不用 Python？

C++ 可以深入练习资源管理、SQLite C API、跨平台进程和网络、构建系统等工程能力；生成的 CLI 运行依赖更少。项目也证明 Agent 工具调用不必依赖 Python AI 框架。

### Q2：为什么不直接让模型输出 SQL？

模型不知道陌生数据库的真实表和字段，而且 SQL 出错时需要反馈闭环。工具调用允许模型按需查看 Schema，并根据真实 SQLite 错误修正。

### Q3：如何保证模型不会删除数据？

不能只相信提示词。文件只读打开，SQLite authorizer 拒绝写入和 DDL，`stmt_readonly` 再检查语句，多条 SQL、ATTACH、危险 PRAGMA 和未知函数也会被拒绝。

### Q4：执行成功是否代表答案正确？

不代表。执行成功只说明 SQL 合法且能运行。项目文档和指标明确区分执行成功、本地结果一致和官方语义评价。

### Q5：为什么 Agentic 会使用更多 tokens？

因为它需要多轮探索和工具反馈。代价是延迟与 token 增加，潜在收益是减少 Schema 幻觉并支持修正。需要更大规模实验才能判断收益是否值得。

### Q6：离线 Demo 是否只是写死结果？

不是。Mock 写死的是模型会发出什么工具调用，数据库结构、SQL 编译、失败、修正后执行和结果读取都是真实 SQLite 行为。

### Q7：为什么现在没有多轮对话？

当前每个问题独立，可避免跨数据库污染和历史无限增长。真实体验说明短期上下文有价值，下一步会增加有上限、切库自动清理的 REPL 会话能力。

### Q8：Windows 是如何支持的？

HTTP 改用 libcurl；Windows 命令行从 UTF-16 转 UTF-8；控制台、中文路径、MSVC flags、Shell32 和 vcpkg 静态 libcurl 都有专门适配，并由 Windows CI 编译和测试。

### Q9：项目最大的工程价值是什么？

把模型规划与数据库权限分离：模型可以灵活决策，但安全边界、资源限制和结果证据由确定性 C++ 代码掌握。

---

## 22. 术语表

| 术语 | 简明解释 |
|---|---|
| Text-to-SQL | 把自然语言问题转换为 SQL |
| Schema | 数据库的表、字段、类型、主外键结构 |
| Agent | 能根据反馈多步决策并调用工具的模型控制流程 |
| Tool Calling | 模型输出结构化工具请求，由程序执行后返回结果 |
| REPL | 读取输入、执行、输出并继续等待的交互循环 |
| Authorizer | SQLite 在执行操作前调用的权限检查机制 |
| Baseline | 完整 Schema 加一次模型生成，不修正 |
| Agentic | 动态探索、执行反馈和有限修正模式 |
| Gold SQL | 数据集提供的标准参考 SQL |
| Result Agreement | 预测和标准 SQL 在当前数据库上的执行结果一致 |
| Test Suite Accuracy | Spider 官方更严格的跨实例 SQL 评价指标 |
| ASan/UBSan | 检测内存错误和未定义行为的编译运行工具 |
