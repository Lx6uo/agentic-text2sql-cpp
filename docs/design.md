# 技术说明

这是单控制器 CLI：`main` 负责参数、REPL 和展示，`database` 管理 SQLite，`llm` 负责 curl 和响应校验，`agent` 执行工具循环，`benchmark` 加载 Spider 并比较结果。使用 `std::function` 在同一个 Agent 中切换真实 API 和 Mock；不包含多 Agent、框架、服务器或本地模型。

## 工具调用与停止

每个问题从新的 system/user 上下文开始。LLM 每轮自行选择零个或多个工具；C++ 保留完整 assistant 消息，按原 `tool_call_id` 加入每个执行结果。三个工具独立可调用，未固定数据库、字段映射或调用顺序。

- `list_tables` 返回真实表和视图。
- `describe_tables` 验证所选表名，读取字段、复合主键序号和外键。标识符用 SQLite 双引号转义。
- `execute_sql` 返回成功标志、SQL、列、类型化行、错误、执行时间、截断、查询编号、超时及取消标志。最多保留 100 行给本地显示，默认仅 10 行进入模型上下文；工具预览另有限制 32 KB。

`max_steps` 按模型请求轮次计算，一轮可有多个工具，另记录 `tool_calls`。默认最多 8 轮、3 次 SQL 执行、2 次错误修正。失败后的下一次 SQL 执行算一次修正；成功后重新查询只计 SQL 尝试。达到执行额度仍可读取 Schema 或给出最终回复；再次要求执行则终止。模型结束且最后一次 SQL 成功才设置 Agent `success=true`。执行成功仅代表 SQL 可执行。

停止原因记录在 `termination`：`completed`、`unable_to_answer`、`step_limit`、`sql_attempt_limit`、`repair_limit`、`context_limit`、`api_error`、`cancelled`。非法工具或参数返回结构化错误供模型改正，仍受轮次上限约束。重复 ID、异常 API 信封或空回复终止。API 不自动重试，避免隐式付费和重复执行；Benchmark 遇到 API 错误保存已有记录并停止。

## SQLite 安全边界

文件连接使用 `SQLITE_OPEN_READONLY`，关闭 URI 模式。设置 defensive、trusted_schema=OFF、query_only=ON，禁用扩展加载；SQL 由 `sqlite3_prepare_v3` 编译，解析尾部以拒绝第二条语句，然后检查 `sqlite3_stmt_readonly`。authorizer 只允许 SELECT、READ、递归查询和明确白名单内的普通计算/聚合/窗口/JSON 函数。写入、DDL、ATTACH/DETACH、事务、PRAGMA 和未知函数均拒绝。只读查询中的 CTE、JOIN 和窗口函数正常使用。

Schema 内部查询短暂放行 authorizer，输入仅来自程序固定查询和经过表名单验证的引用标识符；这个路径不接受模型 SQL。公开执行工具不能获得内部权限。

progress handler 每 1000 条 VM 指令检查 SIGINT 和单查询截止时间，准备及读取完成后也检查截止时间。Schema 内部读取同样受截止时间、最多 10000 条和 1 MB 约束。配置 busy timeout；限制 SQL 16 KB、单值 1 MB、列数 128、表达式深度 100、联合 SELECT 20、VM 指令分配 250000。默认本地结果序列化上限 1 MB，超出返回 `truncated=true`。超时和取消清空不完整行，返回失败。该机制是 SQLite 级防护，不是独立操作系统沙箱；极端恶意或损坏数据库仍受进程和 SQLite 本身边界影响。

## HTTP 与密钥

使用 POSIX `posix_spawnp` 调用系统 curl，不调用 shell。传递参数数组，不解析用户字符串为命令。认证头和请求体写入随机 0700 临时目录内的 0600 文件，命令行只含文件路径；curl 子进程环境移除 `DEEPSEEK_API_KEY`。退出时清理文件、关闭管道并回收子进程。

`curl -q` 禁用自动读取 `.curlrc`，`--globoff` 禁用 URL 展开，仅允许 HTTPS，不跟随重定向。通过连接/请求超时、父进程截止时间和最多 2 MB 响应限制防止无限等待或读取。只接受 2xx，错误响应正文不进入日志。SIGINT 终止等待并回收 curl。API body 包含 `stream=false`、`thinking.type=disabled` 和有工具时的 `tool_choice=auto`。不输出或要求完整思维链。

配置环境变量优先于文件，空环境变量也覆盖文件值。配置解析失败不复述可能含密钥的 JSON 内容。示例密钥为空，实际配置和数据库不提交。

模型收到用户问题、所请求的 Schema 和有限工具预览。提示明确把数据库内容当作不可信数据。模型提示不能保证免受提示注入，但 SQLite 授权和资源限制始终在 C++ 执行层强制实施。

## 本地评价

Baseline 把完整 Schema 和问题发送一次，不使用工具、不修正。Agentic 使用同一 Agent。标准 SQL 在模型执行结束后，单独用于评价，绝不作为 Agent 输入。评价连接限制 100000 行、16 MB 总结果；超限记录为不可比较，不能用截断前缀判定正确。

标准 SQL 独立连接开启历史双引号字符串兼容（Spider 官方示例存在此写法）；预测 SQL 连接始终关闭该兼容，以免列名拼写错误被当作字符串。两者均使用同样的只读 authorizer。

比较列位置和数量，忽略列别名；标准 SQL 有外层 `ORDER BY` 时比较行顺序，否则排序比较保留重复次数。数字使用相对容差 `1e-9`，字符串与数字不同，NULL 只等于 NULL。空结果可能造成偶然一致；某些等价 SQL 会因列顺序而被判不同；浮点容差不代表任意 SQL 语义等价。这个指标仅是本地结果一致率，不是官方 Test Suite Accuracy。

JSONL 保存 metadata、逐题 case 和 summary。summary 的率以各模式实际尝试题数为分母，另列可比较数和请求题数。取消或 API 失败会产生部分结果；必须核对 `cases` 和 `requested_cases_per_mode`。数据文件、路径或数据库等题级错误记录 `case_errors` 并使最终退出码为 1；有效运行中答案不一致仅影响指标。预测导出为每行 `SQL\tdb_id`，API 提前停止的文件只包含已尝试题目，应按 case index 对齐官方 gold。
