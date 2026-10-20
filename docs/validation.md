# 实际验证记录

验证日期：2026-10-21。平台：macOS arm64，Apple Clang 21.0.0，CMake 4.4.4；早期阶段使用独立 CMake 4.1.2，最终验收使用已安装的 CMake。此文件记录实际执行结果，具体耗时随机器变化。

## 构建和测试

实际执行并通过：

```bash
cmake -S . -B build
cmake --build build -j 4
ctest --test-dir build --output-on-failure
./build/text2sql --help
./build/text2sql demo
./build/text2sql demo --repair --verbose

cmake -S . -B build-sanitize -DTEXT2SQL_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitize -j 4
ctest --test-dir build-sanitize --output-on-failure

cmake --install build --prefix /tmp/text2sql-install
cd /tmp
/tmp/text2sql-install/bin/text2sql demo --repair --json
```

普通构建及 ASan/UBSan 构建的 CTest 均为 **2/2 通过**，没有 sanitizer 错误。独立运行测试程序输出 **70 项核心检查通过**、**78 项集成检查通过**。安装后的 Demo 不依赖源码路径。

## Demo 实际结果

| 部门 | 平均工资 |
|---|---:|
| Engineering | 115000 |
| Finance | 95000 |
| Marketing | 82500 |

正常 Mock：4 次模型响应，3 次工具调用，1 次 SQL。修正 Mock：5 次模型响应，4 次工具调用，2 次 SQL，1 次修正。第一次错误为 `no such column: department_name`，第二次 JOIN 聚合成功。终止原因为 `completed`。SQLite 结果来自真实执行。

## 覆盖范围

核心检查覆盖：表/视图和字段、外键、普通 SELECT、JOIN、聚合、NULL、CTE、窗口函数、中文及引用标识符、Schema 输入注入拒绝、非存在路径、语法与字段错误、写操作/DDL/事务/ATTACH/DETACH/PRAGMA/文件函数拒绝、多语句/NUL/SQL 长度限制、执行超时、行和字节截断、取消。Agent 检查覆盖正常与修正、未知工具、参数缺失/类型/JSON 错误、多工具消息 ID 上下文、重复失败和停止上限、缺少密钥、异常模型结构和 token 截断、Baseline、使用不同 Schema 和不同工具顺序。

集成检查通过实际子进程运行产品 CLI，覆盖帮助、Demo、schema、query 参数解析、REPL 全部内部命令、空格和中文路径、EOF、REPL 等待输入时 Ctrl+C、JSON/verbose 分流、无效配置和缺少密钥、数据库字节不变。

HTTP 使用测试专用 curl 替身：验证真实请求结构、环境变量优先、认证文件 0600/目录 0700、密钥不出现在 argv 或 curl 环境、完整 Tool Calling/错误反馈/修正/token 计数、401、非法 JSON/响应结构、传输错误、curl 超时、2 MB 大小限制、父进程超时和 SIGINT 取消；检查所有这些路径的临时目录清理及子进程回收。此验证不涉及真实网络模型。

Spider 格式 fixture 有 3 题，真实临时 SQLite 和 `dev.json`：正确标准答案、结果不一致标准答案、禁止写入的标准答案。两种模式均产生 3 次可执行预测、1 次结果一致、2 次可比较；每种模式本地结果一致率为 1/3。JSONL 含 metadata、6 个 case 和 summary，预测文件逐题导出。另验证已有输出不覆盖、API 错误立即停止并保存部分结果、危险 db_id 拒绝。此数字仅是 fixture 测试结果，**不是 Spider 数据集模型准确率**。

## 外部验证边界

- 已读取官方 DeepSeek API 和 Tool Calling 文档，确认 `deepseek-flash`、非思考、非流式及工具接口。当前未提供真实 API Key；按照开发说明，未进行真实付费调用。真实网络可达性、账号权限和模型效果未实测。
- 已读取 Spider 官方网站和官方 GitHub 数据格式说明，支持官方数据库布局、dev/train_spider/train_others。官方数据包按说明由用户下载，当前未提供；未执行官方全量评价，未报告官方 Test Suite Accuracy。
- 本机 macOS 已验证；Linux 使用 POSIX 接口但未在 Linux 主机实测，Windows 原生不支持。

## Git 和依赖审查

`git diff --check` 通过，第三方 SHA-256 校验通过。`.gitignore` 忽略配置、环境、用户数据库、数据集、结果和构建目录。示例密钥为空，测试仅使用固定虚假密钥。Git 使用本地分阶段提交，未设置或修改远程仓库。
