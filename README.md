# Agentic Text-to-SQL

用自然语言查询已有 SQLite 数据库。DeepSeek Flash 自主选择表结构探索、SQL 执行工具，根据 SQLite 错误有限修正，最终在普通终端显示 SQL 和结果。无需训练、本地模型或数据库服务器。

支持标准输入 REPL、一次性查询、Schema 查看、verbose 和 JSON 输出；提供真正执行 SQLite 的离线 Mock Demo，以及 Spider 1.0 Baseline/Agentic 对照评价。

## 构建与测试

需要 C++20 编译器、CMake ≥ 3.20 和 libcurl ≥ 7.68 的开发包。支持 macOS、Linux 和原生 Windows/MSVC；真实 API 通过 libcurl，不需要系统 `curl` 命令。无需 Python、Node.js、Docker。SQLite 和 JSON 的固定版本源码已包含，CMake 不在线下载依赖。

```bash
# macOS 的 Xcode Command Line Tools 自带 libcurl；缺少 CMake 时：brew install cmake
cmake -S . -B build
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/text2sql --help
./build/text2sql demo
./build/text2sql demo --repair --verbose
```

Ubuntu/Debian 先安装：

```bash
sudo apt-get install cmake g++ libcurl4-openssl-dev
```

Windows 推荐在 “Developer PowerShell for VS 2022” 中使用 vcpkg 静态库，避免运行时遗漏 DLL：

```powershell
$VCPKG_ROOT = "C:\src\vcpkg"
& "$VCPKG_ROOT\vcpkg.exe" install curl:x64-windows-static

cmake -S . -B build -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\text2sql.exe demo --repair --json
```

Windows 入口直接读取 UTF-16 命令行并转换为 UTF-8，控制台设为 UTF-8；中文配置、数据库、数据集和结果路径会按 UTF-8 传入 SQLite 和 `std::filesystem`。

`demo` 明确标记 `Demo Mode (Mock LLM)`，通过 `examples/demo.sql` 内置内存数据库。正常演示使用工具探索 Schema 后执行 JOIN 聚合；`--repair` 先执行不存在的 `department_name` 字段，收到错误后改用真实字段。预设的是模型消息，表结构、错误与结果由 SQLite 实时产生：Engineering 115000、Finance 95000、Marketing 82500。

二进制不依赖源码目录，复制到其他目录仍可运行 Demo。可选安装：`cmake --install build --prefix "$HOME/.local"`。

## DeepSeek 配置

```bash
cp config.example.json config.json
# 在当前终端安全输入密钥（不回显，也不进入命令历史）
read -rs DEEPSEEK_API_KEY
export DEEPSEEK_API_KEY
```

也可在本地 `config.json` 填写 `api.api_key`。环境变量优先，空环境变量也会覆盖文件值。`config.json` 和 `.env` 已忽略；密钥不会写入 API 请求参数或日志。未配置密钥时仍可运行 Demo、Schema 和 REPL 本地命令。程序从当前目录读取可选 `config.json`，或使用 `--config PATH` 指定（指定文件不存在则报错）。

默认官方 Base URL `https://api.deepseek.com`，请求 `/chat/completions`，模型 `deepseek-flash`（DeepSeek V4.1 Flash）。2026-10-21 核对官方 [Chat Completions](https://api-docs.deepseek.com/api/create-chat-completion) 和 [Tool Calling](https://api-docs.deepseek.com/guides/tool_calls/)：非流式、`thinking.type=disabled`、`tool_choice=auto`。不要求输出思维链。可配置 HTTPS Base URL 和模型，不自动探测或改用其他模型。

配置默认上限：8 轮模型调用、3 次 SQL 尝试、2 次错误修正，SQL 执行 5000 ms、本地显示 100 行、模型预览 10 行。可选 `database.max_sql_bytes`（默认 16384）和 `database.max_result_bytes`（默认 1048576）；配置值会验证类型和范围。

## CLI 和 REPL

```bash
./build/text2sql
./build/text2sql --db ./data/company.sqlite
```

以 `/` 开头的输入是内部命令，其余输入是自然语言问题。每个问题启动新的 Agent 上下文。

| 命令 | 作用 |
|---|---|
| `/help` | 帮助 |
| `/open <path>` | 打开已有 SQLite 文件；整段参数作为路径，可使用引号包含空格 |
| `/tables` | 列出表和视图 |
| `/schema` | 字段类型、主键和外键 |
| `/model` | 模型、Base URL 和密钥是否配置 |
| `/verbose on`、`/verbose off` | 切换工具过程输出 |
| `/exit` | 退出 |

```text
/open "data/我的 数据库.sqlite"
/tables
/schema
/verbose on
查询平均工资最高的三个部门
/exit
```

EOF 正常退出；Ctrl+C 取消当前请求/查询并退出，状态码为 130。错误路径打开失败时保留当前已连接数据库。

一次性查询与结构查看：

```bash
./build/text2sql query --db ./data/company.sqlite \
  --question "Which department has the highest average salary?"
./build/text2sql query --db ./data/company.sqlite \
  --question "List employee names with departments" --verbose
./build/text2sql schema --db ./data/company.sqlite
./build/text2sql schema --db ./data/company.sqlite --json
```

数据库读取实际结构，支持不同 Schema；不根据 Demo 字段映射自然语言。输入是已有 SQLite 文件，程序不会创建用户数据库。

JSON 模式示例：

```bash
./build/text2sql query --db ./data/company.sqlite \
  --question "Count employees" --json > results/query.json
./build/text2sql demo --repair --json --verbose
```

一次性调用 stdout 只含一个 JSON 文档，包含 `success`、`answer`、`query`、`termination`、统计和 trace。`query` 含 SQL、columns、rows、error、execution_ms、truncated、query_id 等。行值保留数字、字符串和 NULL；BLOB 输出 `{"blob_hex":"..."}`。错误也以 JSON 返回，日志进入 stderr，无 ANSI。REPL 的 `--json` 为每次操作输出一个 JSON 文档（JSONL），不输出欢迎语和提示符。结果过长时标记截断；纯文本中的长单元格简化显示，JSON 保留已读取的完整值。

退出码：0 成功；1 查询或运行失败；2 参数或配置错误；130 用户取消。REPL 单次操作错误会显示并继续，正常退出仍为 0。SQL 执行成功不等于问题答案已通过语义验证。

## Spider 1.0 Benchmark

从 [Spider 官方网站](https://yale-lily.github.io/spider) 的 Dataset 下载入口获取数据；官方 [GitHub](https://github.com/taoyds/spider) 主要提供评价代码，克隆它不等于获得数据库包。将下载包解压后的内容放入：

```text
data/spider/
├── dev.json
├── train_spider.json       # 可选
├── train_others.json       # 可选
├── tables.json            # 官方元数据；本程序直接读取 SQLite Schema
└── database/
    └── <db_id>/
        └── <db_id>.sqlite
```

不要将完整数据集或用户数据库提交。程序使用 `question`、`db_id`、`query` 字段；标准 SQL 只用于本地评价，不送给模型。开发集是默认 split，`--split train` 对应 `train_spider.json`。

```bash
./build/text2sql benchmark --dataset ./data/spider --split dev --limit 20

./build/text2sql benchmark --dataset ./data/spider --split dev --limit 20 \
  --mode both --output results/spider-dev-20.jsonl \
  --predictions results/spider-dev-20 --json
```

`--mode baseline`：一次发送完整 Schema，生成并执行一个 SQL，不修正。`--mode agentic`（默认）：动态 Schema 探索、工具调用和有限修正。`--mode both` 对每题串行执行两个模式，统计分别输出。

导出 JSONL 包含运行 metadata、逐题结果和末尾 summary，记录 SQL 执行成功率、本地结果一致率、平均模型轮次、SQL 尝试次数、耗时、可获取的 token 使用量以及可比较题数。输出文件默认自动命名，已存在的文件不会覆盖。

默认另导出 `SQL<TAB>db_id` 预测文本；`both` 对上述示例生成 `results/spider-dev-20.baseline.sql` 和 `results/spider-dev-20.agentic.sql`。可与 [官方 Test Suite 评价工具](https://github.com/taoyds/test-suite-sql-eval) 使用；本项目主程序不依赖该工具或 Python。按 case index 对齐同一 split/limit 的标准答案。取消或 API 错误会保存部分结果，务必先检查题数，API 错误不自动重试。

**本地结果一致率不是 Spider 官方 Test Suite Accuracy。** 比较保留重复行，外层 ORDER BY 时保留顺序，其他情况忽略行顺序；列按位置比较，数值容差 `1e-9`。评价结果上限 100000 行/16 MB，截断不判一致。标准答案失败记录为不可比较，率的分母仍为实际尝试的题数。结果一致不能证明跨数据实例的 SQL 语义等价。

## 安全与隐私

SQLite 文件只读打开，使用 prepare_v3、stmt_readonly、authorizer、progress handler、defensive、关闭 trusted schema 和扩展加载共同控制。拒绝写入、DDL、ATTACH/DETACH、未授权 PRAGMA、多条语句和未知 SQL 函数；合法 JOIN、CTE、聚合和窗口函数可用。不是按 `SELECT` 前缀判断安全。详见 [技术说明](docs/design.md)。

默认只发送问题、Schema 和有限工具预览，不上传数据库文件。数据库内容属于不可信数据，提示不允许覆盖系统指令；执行权限始终由 SQLite 强制限制。真实查询会发送工具选中记录的预览，请只查询可以发送给所配置 API 的数据。

libcurl 在当前进程内发送 HTTPS，请求不经过 shell 或子进程。API Key 仅进入内存中的认证头，不写临时文件或命令行；禁用重定向，强制 TLS 证书和主机名校验，并限制连接时间、总时间和解压后的响应大小。API 错误正文不进入日志。

## 目录与依赖

```text
.
├── CMakeLists.txt, README.md, LICENSE, config.example.json
├── src/
│   ├── main.cpp, common.h, config.h, config.cpp
│   ├── database.h, database.cpp
│   ├── llm.h, llm.cpp, agent.h, agent.cpp
│   └── benchmark.h, benchmark.cpp, demo.h.in
├── third_party/
│   ├── sqlite/sqlite3.c, sqlite3.h
│   ├── json/json.hpp, LICENSE.MIT
│   └── README.md, SHA256SUMS
├── examples/demo.sql
├── tests/tests.cpp, benchmark_tests.cpp, cli_tests.cmake
├── .github/workflows/ci.yml
├── docs/design.md, validation.md
├── data/.gitkeep
└── results/.gitkeep
```

项目 MIT；SQLite 3.50.4 为公有领域；nlohmann/json 3.12.0 为 MIT；libcurl 使用 curl license。版本、来源和 SHA-256 校验值见 `third_party/`。没有 AI 框架和 UI 依赖。

## 验证与限制

CTest 覆盖真实 SQLite 安全/查询行为、Mock Agent 修正和停止条件、HTTPS URL 强制、CLI 帮助和 Demo，以及带中文路径的 Spider 格式 fixture、Baseline/Agentic 对照和导出。GitHub Actions 配置了 Ubuntu、macOS 和 Windows/MSVC 三个平台；CI 不读取真实 API Key。

```bash
cmake -S . -B build-sanitize -DTEXT2SQL_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitize -j 4
ctest --test-dir build-sanitize --output-on-failure
```

当前机器的实际验证结果见 [验证记录](docs/validation.md)。libcurl 网络层已完成真实 DeepSeek Tool Calling；官方 Spider dev 前 3 题已跑 Baseline/Agentic 小规模验证。3 题全部来自同一数据库，只证明链路正确，不代表完整 dev 准确率，也不是官方 Test Suite Accuracy。

当前 macOS 已实际验证；Linux 和 Windows 构建由 CI 定义覆盖，本机没有 Windows 环境，需以首次远程 CI 结果作为 MSVC 的最终证据。不支持写数据库、TUI、流式输出、模型训练或官方测试集排行榜提交。SQL 函数白名单、资源上限和严格双引号规则可能拒绝部分合法但超出当前范围的查询。SQLite 防护是进程内机制，极端恶意数据库不等价于被隔离在 OS 沙箱内。终端对齐只做轻量 UTF-8 处理。
