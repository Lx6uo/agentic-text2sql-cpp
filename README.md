# SQLite 自然语言查询：课程精简版

使用真实 AI 把自然语言转换成只读 SQLite SQL。程序固定读取表结构、生成 SQL、执行查询；失败后把 SQL 错误交给 AI 修正，总共最多执行三次。启动后直接接收自然语言问题。支持 JSON 配置、Windows/macOS/Linux、离线纠错 Demo，以及 Baseline/纠错模式的 CSV 对照评测。

代码使用普通函数、结构体、数组、判断、循环和适量简单指针。各功能保留独立 CPP，没有自写头文件或测试目录。阅读顺序与指针释放说明见 [基础代码说明](docs/guides/basic-cpp.md)。

## 构建和运行

clone 后已经包含样例数据库和完整 Spider 运行数据，编译后自动连接样例库。无需额外下载数据或使用 Git LFS。

需要 C++17、CMake ≥ 3.20、libcurl ≥ 7.68。SQLite 3.50.4 和 cJSON 1.7.19 的固定源码随项目提供，CMake 不在线下载依赖。运行不需要 Python、Node.js 或数据库服务器。

```bash
cmake -S . -B build
cmake --build build -j 4
./build/text2sql
./build/text2sql demo
./build/text2sql --help
```

macOS 使用 Xcode Command Line Tools 自带的编译器和 libcurl，缺少 CMake 时可通过 Homebrew 安装。Ubuntu/Debian：

```bash
sudo apt-get install cmake g++ libcurl4-openssl-dev
```

Windows 在 VS 2022 Developer PowerShell 中使用 vcpkg：

```powershell
$VCPKG_ROOT = "C:\src\vcpkg"
& "$VCPKG_ROOT\vcpkg.exe" install curl:x64-windows-static
cmake -S . -B build -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release --parallel
.\build\Release\text2sql.exe
.\build\Release\text2sql.exe demo
```

Windows 保留原生 UTF-16 参数转 UTF-8、UTF-8 控制台、中文路径、MSVC 和静态运行库适配。

## JSON 配置

复制示例后编辑本地配置：

```bash
cp config.example.json config.json
```

`config.json` 支持 `api`、`agent`、`database` 三个部分；可以省略字段并使用默认值。

```json
{
  "api": {
    "base_url": "https://api.deepseek.com",
    "model": "deepseek-flash",
    "api_key": "",
    "timeout_seconds": 45,
    "max_tokens": 2048
  },
  "agent": { "max_sql_attempts": 3 },
  "database": {
    "path": "data/company.sqlite",
    "timeout_ms": 5000,
    "max_display_rows": 100,
    "max_sql_bytes": 16384,
    "max_result_bytes": 1048576
  }
}
```

也可从 `DEEPSEEK_API_KEY` 环境变量读取密钥。环境变量优先，空环境变量也覆盖文件值。macOS/zsh 可安全输入，不回显且不把密钥写入命令历史：

```bash
read -rs DEEPSEEK_API_KEY
export DEEPSEEK_API_KEY
```

默认读取当前目录的可选 `config.json`。指定配置：

```bash
./build/text2sql --config "配置/我的配置.json"
```

指定文件不存在或配置非法时会报错。配置和环境文件已被 `.gitignore` 排除；错误信息不会输出配置内容或密钥。

`max_sql_attempts` 可降低执行次数，程序始终限制为最多三次。旧配置中的 `max_steps`、`max_repairs` 仍会保留更低的次数限制；`max_preview_rows` 兼容读取，但当前流程不上传查询行预览。Baseline 始终最多执行一次。

## 直接输入自然语言

启动后显示已连接数据库、示例问题和输入提示：

```text
SQLite 自然语言查询
已连接：company.sqlite
直接输入问题并按回车，每次查询后可以继续提问。
例如：查询「employees」表的记录总数。
输入 /help 查看引导，输入 /exit 退出。

你：查询平均工资最高的部门
```

数据库可以在 JSON 的 `database.path` 中配置，或通过 `--db` 指定，参数优先：

```bash
./build/text2sql --db "data/company.sqlite"
```

配置中的相对数据库路径以配置文件所在目录为基准。没有提供路径时，默认连接随程序构建的样例数据库，随后持续接收自然语言问题。已连接的数据库在整个会话中保留，每个问题重新建立 AI 上下文；查询结果或错误显示后继续提示 `你：`。

输入 `/demo` 无需 API Key 即可体验本地 SQL 纠错，`/help` 查看引导，`/exit` 退出。启动连接或配置失败会报告错误；单个问题失败后可以继续提问。EOF 正常退出，Ctrl+C 取消请求或 SQL 并返回 130。

离线演示使用 `./build/text2sql demo`。Demo 不需要 API Key 或联网，先执行错误字段 `department_name`，随后修正 JOIN 聚合。SQLite 实际返回 Engineering 115000、Finance 95000、Marketing 82500；内置数据与 `examples/demo.sql` 对应。

## Benchmark 和 CSV

完整 Spider 已包含在 `data/spider.zip`。CMake 配置时自动解压到 `build/data/spider`，其中包含全部开发/训练/测试题目、206 个不重复 SQLite 库、表元信息和标准 SQL。数据文件来源与校验清单见 [数据说明](data/README.md)。

```text
仓库 data/
├── company.sqlite
├── sample-spider/          # 2 道中文题及对应数据库
├── spider.zip             # 约 57 MiB，普通 Git 文件
├── spider-manifest.json
└── SHA256SUMS
```

构建结果中的 `data/` 包含展开后的数据。复制程序时同时复制旁边的 `data/`；安装时程序和数据会一起安装。

通过独立命令启动评测，数据集目录留空使用内置完整 Spider，再输入划分（默认 dev）、题数（默认 20）和 CSV 路径（留空自动生成）：

```bash
./build/text2sql benchmark
# 指定 JSON 配置
./build/text2sql --config config.json benchmark
```

支持 dev、train_spider、train_others 和 test；train 是 train_spider 的别名。

每题串行比较两种方式：

- `baseline`：AI 生成并执行一次 SQL。
- `corrected`：相同问题与结构，失败后修正，总共最多三次执行。

CSV 为 UTF-8，字段为 `question,db_id,mode,sql,executed,matched,error`。成功标记为 1，失败为 0；标准 SQL 失败、评测结果截断或无法比较时，`matched` 留空并记录错误。文本字段正确引用逗号、双引号和换行，已存在的输出文件不会覆盖。

终端汇总每种模式的题数、执行成功率和结果一致率。分母是该模式实际尝试的题数。API 或输出错误会停止评测，并保留已写入的 CSV；请检查实际行数。

结果按列位置比较。标准 SQL 有外层 ORDER BY 时保留顺序，否则排序行号后比较并保留重复行。数值使用相对容差 1e-9，截断结果不判一致。评测最多读取 100000 行/16 MB。标准 SQL 只用于本地比较，不发送给模型。

这是本地结果一致率，不是 Spider 官方 Test Suite Accuracy。

## 执行边界和目录

SQLite 文件只读打开；保留 authorizer、query_only、defensive、trusted_schema=OFF、多语句拒绝、函数白名单、查询超时和结果大小上限。合法 JOIN、CTE、聚合和窗口函数可以使用。HTTPS 请求在进程内由 libcurl 发送，校验证书、不跟随重定向，限制时间与解压后的响应大小，密钥不进入日志或命令行。

模型接收问题、表结构和 SQL 执行错误，系统提示词使用中文。程序不上传数据库文件或查询结果行，不支持写数据库。API Key 是使用者的本地配置，不随仓库提供；无需密钥的 `/demo` 始终可用。

```text
src/
├── main.cpp                 # 启动和 Windows 参数适配
├── common.cpp, config.cpp   # 结构体、JSON 配置
├── database.cpp, agent.cpp  # SQLite、固定纠错
├── llm.cpp, json.cpp        # HTTPS、JSON 协议
├── benchmark.cpp, files.cpp # CSV 评测、文件读取
└── cli.cpp, demo.cpp        # 查询输入、演示数据
```

`main.cpp` 按顺序包含模块，CMake 只编译入口。第三方库自带的头文件保留，版本和校验值见 `third_party/`。随仓库数据由 `.gitignore` 明确放行，本地密钥、用户数据库、解压数据及结果文件继续忽略。GitHub Actions 保留三个平台的构建和离线 Demo。

文档总入口见 [文档导航](docs/README.md)。当前实际验证见 [验证记录](docs/verification/current.md)，更多执行细节见 [技术说明](docs/guides/design.md)。当前展示材料见 [项目报告](docs/report/project-report.md)、[PDF 打印版](docs/report/project-report.pdf) 和 [7 页汇报提纲](docs/report/presentation-outline.md)，内容对应 C++17、cJSON 与固定纠错流程。

项目 MIT；SQLite 为公有领域，cJSON 为 MIT，libcurl 使用 curl license。
