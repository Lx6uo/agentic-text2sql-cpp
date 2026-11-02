# 历史验证记录（归档）

> 用途：追溯旧版本验证。这里的菜单、动态 Tool Calling、CTest、JSONL 和旧命令只对应当时的实现；当前使用方式与结果请看 [当前验证记录](../verification/current.md)。

## 历史版本：直接自然语言输入

启动后直接显示数据库连接、引导和 `你：` 提示，没有数字菜单。数据库由 `--db` 或 JSON 的 `database.path` 自动连接；配置中的相对路径以配置目录为基准。未设置时只询问一次路径，随后持续接收问题。Benchmark 使用独立启动命令，Demo 和 Windows 支持保留。

本次 C++17 构建成功，10 项交互检查通过，覆盖自动连接、配置相对路径、参数优先、引导、退出、启动错误、缺少密钥和离线 Demo。实际连续输入两个中文问题并完成真实 AI 查询：员工总数为 6，各部门平均工资与样例一致，数据库 SHA-256 未改变。Windows 参数转换实现核对保留。

体验窗口已重新启动并自动连接样例库；体验构建和数据库放在源码目录外。项目 CPP 为 1823 行，没有自写头文件或测试目录。

## 历史版本：课程精简版，保留 JSON 配置和 Windows

项目自己的 CPP 从 3497 行降到 **1785 行**，减少 1712 行（约 49%）。查询改为固定的 Schema → SQL → 执行/纠错流程，总共最多三次；交互为简单菜单，Benchmark 输出 Baseline/纠错模式的 CSV 和汇总。JSON 配置、Windows/macOS/Linux 适配、只读限制、超时、结果上限和基础语法保留。

本次实际验证：

- C++17 普通与 ASan/UBSan 构建成功，临时目录中的 **50 项检查通过**，没有 sanitizer 错误。覆盖只读与多语句限制、超时、取消、动态数组、64 位整数、NULL/BLOB/NUL 文本、结构读取、排序比较、固定纠错及三次硬上限。
- **10 项菜单和配置检查通过**：菜单/独立 Demo、JSON 配置和中文路径、旧配置更低限制、非法配置不输出敏感值、CSV 拒绝覆盖已有文件，以及无测试目标的构建。
- 两个中文样例问题真实运行 Baseline 和纠错模式，均为 **2/2 执行成功、2/2 与标准结果一致**。CSV 为四条完整记录，SQLite 文件 SHA-256 未改变。
- Windows UTF-16 参数转换实现与上一版本一致，MSVC、UTF-8、Shell32 和静态运行库设置均保留；本次运行验证在 macOS arm64 完成，没有实际运行 Windows。
- 自写 CPP 语法检查、第三方 SHA-256 和 `git diff --check` 通过。没有恢复自写头文件或测试目录。临时构建、样例库、CSV 和检查程序验证后清除，源码目录保持干净。

上述样例只验证流程，不代表完整 Spider 准确率。以下为先前版本的历史记录。

## 历史版本：等级 2，适当使用简单指针

本次重写把数据库查询、模型回复、Agent 和 Benchmark 结果改成普通结构体；行数据使用动态数组，通过简单指针传递。普通错误用返回值表示，自写源码不使用引用参数、异常、三元表达式、模板定义、Lambda、智能指针、`vector/map/set`、`auto` 或 `.get<T>()`。JSON 依赖改为固定版本 cJSON 1.7.19；SQLite 和中文系统提示词继续保留。

实际验证结果：

- C++17 普通构建和 ASan/UBSan 构建成功。
- 临时目录中的 **56 项检查全部通过**，没有 sanitizer 错误。覆盖只读限制、语句释放、超时、取消、动态数组扩容、64 位整数 JSON 精度、NULL/BLOB/含 NUL 文本、结果排序比较、Schema 注入拒绝、打开失败保留连接，以及 Mock 纠错和停止条件。
- **7 项 CLI 检查通过**，覆盖正常与纠错 Demo、参数错误、配置错误不输出敏感字段、JSONL REPL 和无测试目标的构建。
- 两个中文问题使用真实 AI 运行 Baseline/Agentic 对照；两种模式均为 **2/2 执行成功、2/2 与标准结果一致**。JSONL 和预测文件各项数正确，查询前后 SQLite SHA-256 一致。
- 全部项目 CPP 的语法扫描和第三方 SHA-256 校验通过。没有恢复自写头文件或测试目录，验证材料均放在源码目录外，验证后清除。

这些真实 AI 问题来自临时样例数据库，用于验证重写后的流程，并不代表完整 Spider 准确率。以下为先前版本的历史记录。

## 历史版本：移除测试目录，使用中文系统提示词

项目已移除 `tests/` 目录、CMake 测试目标和 CI 中的 CTest 步骤。Agent 与 Baseline 的系统提示词已改为中文，保留只读 SQL、真实 Schema、结果列数和行数、有限纠错等约束。

本次在源码目录外完成 C++17 构建，只生成主程序和 SQLite 依赖库。离线 SQLite 纠错 Demo、Schema 查看与 CLI 帮助均验证成功。使用临时样例数据库进行一题真实 AI 对照评测，Agentic 和 Baseline 都执行成功并与标准结果一致，员工总数为 6；查询前后数据库 SHA-256 一致。

临时验证的源码外构建、数据库和结果已经清除。以下内容均为移除测试目录前的历史记录，其中的 CTest 命令和测试程序对应当时的版本；当前构建及运行方式以 README 为准。

## 移除测试前：仅 CPP 的分模块版本

当前项目按功能拆成 9 个 CPP，`main.cpp` 为 156 行，只保留入口和主流程。`common.cpp` 保存共用结构体和声明，各模块由入口统一包含；项目没有自写或生成的头文件。

本次在源码目录外完成 C++17 构建，CTest **5/5 通过**，包括 **77 项核心检查**和 **21 项 Benchmark 检查**。在不同工作目录运行离线纠错 Demo，SQLite 先返回字段不存在，再完成正确查询，结果为 Engineering 115000、Finance 95000、Marketing 82500。

所有功能 CPP 扫描继续满足 B 级语法要求；`demo.cpp` 中内置 SQL 与 `examples/demo.sql` 一致。以下为此前基础版本的验证记录。

## 移除测试前：B 级基础 C++ 版本

当前重构版本在 macOS arm64 / Apple Clang 21 / libcurl 8.7.1 上验证：

- C++17 普通构建 `build-basic`：CTest **5/5 通过**。
- ASan/UBSan 构建 `build-basic-sanitize`：CTest **5/5 通过**，没有 sanitizer 错误。
- 两种构建均完成 **77 项核心检查**和 **21 项 Benchmark 检查**。新增检查覆盖打开失败保留原连接、查询语句释放、显式关闭连接，以及关闭后的查询和 Schema 错误。
- 离线纠错 Demo 先收到 `no such column: department_name`，随后真实 SQLite 查询成功，返回 Engineering 115000、Finance 95000、Marketing 82500；2 次 SQL 尝试、1 次修正。
- 真实 DeepSeek 查询演示数据库员工总数，返回 `6`，终止原因为 `completed`，消耗 3766 tokens。查询前后 SQLite 文件 SHA-256 一致。
- 自写 `src/` 和 C++ 测试源码扫描未发现类定义、模板定义、Lambda、智能指针、`std::function`、`vector/map/set`、`auto` 类型推导或范围 `for`。库内部实现和 JSON 的模板接口调用继续保留。
- `git diff --check` 通过。此次验证在本地 macOS 完成；下面的远端 CI 结果属于先前版本。

```bash
cmake -S . -B build-basic
cmake --build build-basic -j 4
ctest --test-dir build-basic --output-on-failure
./build-basic/text2sql demo --repair --verbose

cmake -S . -B build-basic-sanitize -DTEXT2SQL_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-basic-sanitize -j 4
ctest --test-dir build-basic-sanitize --output-on-failure
```

以下为先前版本的历史验证：日期 2026-10-21。平台：macOS arm64，Apple Clang 21.0.0，CMake 4.4.4，libcurl 8.7.1。具体耗时随机器变化。

## 构建和测试

实际执行并通过：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/text2sql_tests
./build/benchmark_tests
./build/text2sql --help
./build/text2sql demo --repair --verbose

cmake -S . -B build-sanitize -DTEXT2SQL_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitize -j 4
ctest --test-dir build-sanitize --output-on-failure
```

切换 libcurl 并加入跨平台适配后，普通构建及 ASan/UBSan 构建的 CTest 均为 **5/5 通过**，没有 sanitizer 错误。独立运行测试程序输出 **72 项核心检查通过**、**21 项跨平台 Benchmark 检查通过**。

## Demo 实际结果

| 部门 | 平均工资 |
|---|---:|
| Engineering | 115000 |
| Finance | 95000 |
| Marketing | 82500 |

正常 Mock：4 次模型响应，3 次工具调用，1 次 SQL。修正 Mock：5 次模型响应，4 次工具调用，2 次 SQL，1 次修正。第一次错误为 `no such column: department_name`，第二次 JOIN 聚合成功。终止原因为 `completed`，SQLite 结果来自真实执行。

## 覆盖范围

核心检查覆盖表/视图和字段、外键、SELECT、JOIN、聚合、NULL、CTE、窗口函数、中文及引用标识符、Schema 输入注入拒绝、非存在路径、语法与字段错误、写操作/DDL/事务/ATTACH/DETACH/PRAGMA/文件函数拒绝、多语句/NUL/SQL 长度限制、执行超时、行和字节截断、取消。Agent 检查覆盖正常与修正、未知工具、参数缺失/类型/JSON 错误、多工具上下文、重复失败和停止上限、缺少密钥、异常模型结构和 token 截断、Baseline、不同 Schema 与工具顺序。

便携测试覆盖 CLI 帮助和修正 Demo，以及带中文目录名的 Spider fixture、真实临时 SQLite、两种模式、JSONL/预测导出、结果比较、已有输出保护和数据库字节不变。核心检查还覆盖配置、缺少密钥和非 HTTPS Base URL 拒绝。

## libcurl 真实 API

HTTP 已从系统 curl 子进程迁移为 libcurl。macOS 上使用真实 DeepSeek API 完成 `list_tables → describe_tables → execute_sql → final answer`，查询每个部门人数，结果为 Engineering、Finance、Marketing 各 2 人，消耗 3849 tokens。

认证头只在进程内存中，不再存在 argv、子进程环境或临时认证文件。超时、TLS、HTTPS-only、重定向禁用、2 MB 解压后响应上限和取消逻辑由 libcurl option/callback 实现。没有把真实密钥写入文档、结果或提交。

## Spider

便携 Spider fixture 有 3 题，两种模式均产生 3 次可执行预测和 3 次结果一致，主要验证跨平台文件、中文路径、数据库和评价流水线。

官方 Spider 1.0 已从开发说明中的官方链接下载：166 个 SQLite 数据库、dev 1034 题、train_spider 7000 题。切换 libcurl 后对官方 dev 第 1 题运行 Baseline/Agentic，二者均执行成功并与 gold 一致；此前也运行过官方 dev 前 3 题并全部一致。这些题都来自 `concert_singer`，只证明真实链路可用，**不是完整 Spider 准确率或官方 Test Suite Accuracy**。

## Windows 与 CI 边界

- 产品已移除 POSIX curl 子进程、文件描述符式 Benchmark 输出等平台依赖。
- Windows 使用 `CommandLineToArgvW` 读取 UTF-16 参数并转换为 UTF-8，设置 UTF-8 控制台；SQLite 接收 UTF-8 路径，配置/数据集/结果使用 `std::filesystem::path`。
- CMake 支持 MSVC flags、Shell32 和 vcpkg `curl:x64-windows-static`；CTest 中的核心、Benchmark、帮助和 Demo 测试不依赖 POSIX。
- `.github/workflows/ci.yml` 定义 Ubuntu、macOS 和 Windows/MSVC 三个平台。
- 公开仓库首次实际验证中暴露并修复了 Win32 头文件顺序、MSVC 静态运行库和 nlohmann/json 初始化歧义。修复后的远端运行 [39551393403](https://github.com/Lx6uo/agentic-text2sql-cpp/actions/runs/39551393403) 三个平台全部通过；每个平台均完成配置、编译、5 项 CTest 和离线修正 Demo。

## Git 和依赖审查

`git diff --check` 和第三方 SHA-256 校验通过。`.gitignore` 忽略配置、环境、用户数据库、Spider 数据、结果和构建目录。示例密钥为空。SQLite 和 JSON 固定源码随仓库提供；libcurl ≥ 7.68 由系统或包管理器提供，不在 CMake 配置阶段下载。
