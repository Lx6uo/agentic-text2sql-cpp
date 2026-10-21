# 实际验证记录

验证日期：2026-10-21。平台：macOS arm64，Apple Clang 21.0.0，CMake 4.4.4，libcurl 8.7.1。此文件记录实际执行结果，具体耗时随机器变化。

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

切换 libcurl 并加入跨平台适配后，普通构建及 ASan/UBSan 构建的 CTest 均为 **5/5 通过**，没有 sanitizer 错误。独立运行测试程序输出 **71 项核心检查通过**、**21 项跨平台 Benchmark 检查通过**。

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
- 当前仓库没有 remote，工作流尚未在远程 Windows runner 实际执行。本机 macOS 已验证，MSVC 通过需以首次远程 CI 作为最终证据。

## Git 和依赖审查

`git diff --check` 和第三方 SHA-256 校验通过。`.gitignore` 忽略配置、环境、用户数据库、Spider 数据、结果和构建目录。示例密钥为空。SQLite 和 JSON 固定源码随仓库提供；libcurl ≥ 7.68 由系统或包管理器提供，不在 CMake 配置阶段下载。
