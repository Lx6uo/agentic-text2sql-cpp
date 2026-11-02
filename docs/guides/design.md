# 课程精简版技术说明

> 用途：解释当前查询流程、数据库执行控制和评测规则。代码入门见 [代码阅读说明](basic-cpp.md)，实际验证见 [当前验证记录](../verification/current.md)。

项目使用多个 CPP 和普通结构体，`main.cpp` 统一包含模块。默认启动直接进入自然语言输入，会话复用数据库连接，各问题重建模型上下文；Benchmark 使用独立命令。保留 JSON 配置、Windows 适配、SQLite、真实 API、固定纠错和 CSV Benchmark；不包含自写头文件或测试目录。

## 固定查询流程

`run_query` 先读取真实表结构，包括字段类型、非空约束、主键及外键，再用中文系统提示词要求 AI 返回一条只读 SQL。普通会话消息包含问题、结构和后续 SQL 错误，没有工具定义、tool_call_id、token 统计或 trace。

成功执行 SQL 就停止。执行失败时把错误作为新的用户消息交给模型；模型调用与 SQL 尝试总共最多三次，Baseline 只尝试一次。API/回复错误停止，不自动重试网络请求。旧配置中的较低轮数、执行次数或纠错上限仍会限制最终次数。

AI 输入不含标准答案 SQL 或查询结果行。系统提示词将数据库结构视为数据；执行权限由 SQLite 控制。SQL 执行成功不代表问题语义一定正确。

## SQLite 和资源限制

用户文件以 SQLITE_OPEN_READONLY 打开，Demo 单独创建内存库后切为只读。保留 query_only、defensive、trusted_schema=OFF、扩展加载禁用、语句只读检查、多语句拒绝和函数白名单；写入、DDL、ATTACH、事务、PRAGMA 和未知函数被拒绝。CTE、JOIN、聚合与窗口函数继续可用。

内部 Schema 查询只使用固定 SQL 和转义后的真实表名，临时放行 authorizer，随后恢复。查询准备、执行和结果读取都受截止时间约束，progress handler 检查取消和超时。SQL 长度、表达式、列数、单值和总结果均有限制；截断标记保留，错误清除不完整行。

查询结果保留 SQLite 类型、整数、小数、文本、NULL 和 BLOB。动态数组通过普通指针传递，调用方显式释放；结构体不定义构造/析构函数或成员函数。

## JSON 配置、HTTP 与 Windows

配置支持 api、agent、database 三个部分，字段类型与范围被检查，环境变量优先于文件，包括空环境变量。`database.path` 指定启动数据库，相对路径以配置目录为基准；`--db` 参数优先。未指定时连接内置样例库；默认数据优先从程序旁边查找，支持 Windows 多配置构建和安装布局，最后回退到构建目录。错误不输出 JSON 内容或密钥。

libcurl 在进程内发送 HTTPS，禁止重定向，校验证书链和主机名，限制连接时间、总时间和解压后 2 MB 响应，忽略 netrc，并检查取消。认证头仅在内存中，不进入 argv、临时文件或日志；API 错误正文不输出。JSON 由固定版本 cJSON 处理。

Windows 使用 CommandLineToArgvW 和 WideCharToMultiByte，把原生 UTF-16 参数转为 UTF-8，并设置 UTF-8 控制台。文件流使用 C++17 filesystem，SQLite 接收 UTF-8 路径；MSVC、Shell32、vcpkg 静态 curl 和静态运行库配置保留。

## CSV Benchmark

使用相同问题、同一数据库，依次运行 Baseline 和纠错模式。生成预测后，才在本地独立只读连接执行标准 SQL。标准连接支持旧 Spider 双引号字符串写法，预测连接继续严格检查标识符。

评测连接最多读取 100000 行/16 MB。比较列位置与数量，外层 ORDER BY 存在时比较行顺序，否则用 O(n log n) 归并排序排序行号，保留重复次数。数字使用相对容差 1e-9，字符串与数字不同，NULL 只等于 NULL。截断或失败时不判一致。

CSV 每行包含问题、db_id、模式、预测 SQL、执行成功标记、一致标记和错误。文本引用逗号、引号和换行，已存在的输出拒绝覆盖。无法比较时一致标记留空；数据库、参考或评测查询错误也记录在 CSV。API 或文件写入错误停止并保留已有记录。

终端汇总各模式实际题数、执行成功率和本地结果一致率。该结果不是 Spider 官方 Test Suite Accuracy，也不能证明 SQL 跨数据实例语义等价。

## 随 clone 提供的数据

样例库以 SQLite 文件直接加入 Git，完整 Spider 运行数据保存在约 57 MiB 的 ZIP 文件，保留题目、数据库、元信息和标准 SQL 原始字节。CMake 根据压缩包 SHA-256 判断是否需要解压，解压位置为构建目录，安装时一并安装。Benchmark 默认使用解压后的完整 Spider；也附带两题样例数据。上游说明、范围和校验清单见 `data/README.md`。
