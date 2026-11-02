# 基于 C++17 与固定纠错的 SQLite 自然语言查询系统

> **当前版本项目报告 · 汇报建议 3～5 分钟**<br>
> 源码基准：`9ae5440`；报告更新：2026-11-02。<br>
> 项目地址：https://github.com/Lx6uo/agentic-text2sql-cpp

**摘要：** 面向 Spider 1.0 的跨数据库 Text-to-SQL 任务，本项目采用 C++17、SQLite、cJSON 和 libcurl，实现从自然语言问题到只读 SQL 查询的轻量终端工具。程序先读取真实数据库结构，再请求 AI 生成 SQL；执行失败时反馈 SQLite 错误，总共最多尝试三次。项目保留 JSON 配置与 Windows/macOS/Linux 适配，内置样例库和完整运行数据，并提供 Baseline/固定纠错的 CSV 对照评测。当前验证用于确认构建、查询、纠错、数据包装与评价流程；尚不能据此判断完整 Spider 准确率或纠错策略的统计收益。

**关键词：** Spider 1.0；Text-to-SQL；C++17；SQLite；固定纠错；CSV 评测

## 1. 任务背景与数据集

### 1.1 Text-to-SQL 任务

用户通常需要掌握 SQL、表名、字段和关联关系才能查询数据库。Text-to-SQL 将这一过程转化为：

```text
自然语言问题 + 真实数据库结构 → 预测 SQL → 数据库查询结果
```

跨数据库任务中的主要难点是表字段匹配、跨表关联、聚合与筛选，以及输出列和行数是否符合问题。SQL 能成功执行，只说明语法和执行条件满足，并不能证明语义正确。

### 1.2 Spider 1.0 与当前内置数据

Spider 1.0 原始论文描述了 **10,181 个问题、5,693 条 SQL、200 个数据库和 138 个领域**。这些是论文的基准规模；本仓库随附运行包的实际划分如下，二者采用不同的数据发布口径。

| 内置划分 | 问题数 | 数据库数 | 项目中的用途 |
|---|---:|---:|---|
| dev | 1,034 | 20 | 默认对照评测 |
| train_spider | 7,000 | 140 | 可选运行数据 |
| train_others | 1,659 | 6 | 可选运行数据 |
| test | 2,147 | 40 | 当前本地包提供的测试划分 |

运行包整合 **206 个不重复 SQLite 数据库**：166 个开发/训练库，加 40 个额外测试库。题目保留 `question`、`db_id` 和 `query`；标准 SQL 仅用于本地比较，不发送给模型。原始元信息和来源说明也随包保存。

样例库包含 3 个部门、6 名员工，另附 2 道中文评测题。完整运行包以约 **57 MiB** 的普通 Git ZIP 文件保存，CMake 自动解压到构建目录，无需额外下载数据或使用 Git LFS；数据库、题目和元信息均有 SHA-256 清单。

<div class="page-break"></div>

## 2. 项目目标与实现选择

目标是在基础 C++ 写法下提供可理解、可构建、可实际执行的自然语言数据库查询系统。

| 目标 | 当前实现 |
|---|---|
| 适配不同数据库 | 程序提前读取字段类型、主键、非空约束和外键 |
| 使用真实 AI | libcurl 请求配置的模型，默认 `deepseek-flash` |
| 处理执行错误 | 反馈错误并修正，最多 3 次 SQL 尝试 |
| 限制数据库操作 | SQLite 只读连接、授权控制和资源上限 |
| 保留评测能力 | Baseline/固定纠错对照，CSV 与终端汇总 |

程序使用普通函数、结构体、字符串、数组和简单指针，业务错误通过返回值传递。功能按多个 `.cpp` 拆分，由 `main.cpp` 统一包含；项目没有自写头文件或测试目录。SQLite 3.50.4 和 cJSON 1.7.19 固定源码随仓库提供，构建标准为 C++17，网络层使用 libcurl ≥ 7.68。

## 3. 固定纠错设计

### 3.1 查询控制流程

```text
用户输入问题
     ↓
程序读取真实 SQLite 表结构
     ↓
AI 返回一条只读 SQL
     ↓
SQLite 执行 ── 成功 ──→ 分区显示 SQL 和查询结果
     │
     └── 失败 → 把 SQL 错误交给 AI → 修正后再次执行
                    总共最多 3 次尝试
```

模型接收中文系统提示词、问题和数据库结构。当前流程使用普通 Chat Completions 消息；生成 SQL 后由 C++ 执行，模型不选择工具。每个问题使用新上下文；同一问题的失败 SQL 和错误在纠错期间保留。API 或回复格式错误立即停止，不自动重试网络请求。

### 3.2 执行限制与两种评测模式

| 项目 | 默认行为 |
|---|---|
| Baseline | 生成并执行一次 SQL，失败后不修正 |
| 固定纠错（CSV 标记 `corrected`） | 最多 3 次执行，即首次查询加最多 2 次修正 |
| SQL 执行时间 | 5,000 ms |
| 本地显示上限 | 100 行、约 1 MiB 结果 |

SQLite 采用只读连接、授权检查、函数白名单和资源上限，拒绝写入、DDL、多语句及危险操作；JOIN、CTE、聚合和窗口函数可用。执行权限由程序强制控制。

<div class="page-break"></div>

## 4. 使用方式与运行效果

### 4.1 构建后直接提问

```bash
cmake -S . -B build
cmake --build build -j 4
./build/text2sql
```

默认启动连接内置样例库，显示引导和 `你：` 输入提示。输入问题并回车后，SQL 和结果分区显示，随后可以继续提问。`/help` 查看引导，`/demo` 离线演示，`/exit` 退出。

JSON 配置保留 API、次数与数据库资源选项；`database.path` 可以指定数据库，`--db` 参数优先。Windows 保留 UTF-16 参数转 UTF-8、中文路径、控制台和 MSVC 构建适配。真实 AI 查询需要使用者自己的 API Key；密钥不随仓库分发。

### 4.2 离线纠错演示

离线 Demo 的模型回复为预设数据，数据库操作由 SQLite 真实执行。第一次 SQL 使用不存在的字段 `department_name`，收到 `no such column: department_name` 后，第二次改为正确的 JOIN 聚合。

```text
SQL 语句
--------------------------------
SELECT d.name AS department,
       ROUND(AVG(e.salary), 2) AS average_salary
FROM employees e
JOIN departments d ON e.department_id=d.id
GROUP BY d.id,d.name
ORDER BY average_salary DESC LIMIT 3

查询结果
--------------------------------
department   average_salary
Engineering  115000
Finance      95000
Marketing    82500
3 行
--------------------------------
SQL 尝试次数：2
```

上面的 SQL 为便于排版换行，逻辑与当前 Demo 一致。该演示证明执行错误能够触发下一次修正，以及结果来自 SQLite；它不评价真实模型的生成能力。

### 4.3 CSV 对照评测

```bash
./build/text2sql benchmark
```

数据集路径留空使用内置 Spider，再输入划分、题数和 CSV 路径。每题串行执行 Baseline 和固定纠错两种模式，CSV 记录问题、数据库、模式、预测 SQL、执行成功、一致标记和错误。终端汇总各模式实际题数、执行成功率和本地结果一致率。

<div class="page-break"></div>

## 5. 当前版本验证与评价边界

### 5.1 评价口径

生成预测后，在独立只读连接中执行标准 SQL。结果按列位置比较；标准 SQL 有外层 ORDER BY 时保留行顺序，否则忽略顺序并保留重复次数。数字采用相对容差 1e-9。评测上限为 100,000 行、16 MiB；失败或截断结果不判一致，CSV 一致标记留空并记录错误。

执行成功率与结果一致率的分母均为各模式实际尝试题数。本地结果一致率不等于 Spider 官方 Test Suite Accuracy，也不能证明不同数据库实例上的语义等价。

### 5.2 功能回归结果

本次按当前源码重新执行两组真实 AI 回归，原始 CSV 保存在 `docs/report/evidence/`。样本较小，仅用于确认查询与评价链路：

| 验证数据 | 每种模式题数 | Baseline 执行 / 一致 | 固定纠错执行 / 一致 |
|---|---:|---:|---:|
| 内置中文样例库 | 2 | 2/2 · 2/2 | 2/2 · 2/2 |
| 官方 Spider dev 首题 | 1 | 1/1 · 1/1 | 1/1 · 1/1 |

两组题目不合并为泛化准确率。当前记录还确认了无密钥离线 Demo、206 个数据库及文件校验、干净本地 clone 后构建、默认连接、安装目录迁移与 CSV 输出。Windows 适配和三平台 CI 配置保留，当前版本的实际运行验证在 macOS 完成。

没有完整 dev 集的当前版本评测，也没有证明纠错模式优于 Baseline 的统计实验。旧动态 Agent 的 20 题结果属于不同实现，保存在历史验证记录中，不作为本报告的当前性能指标。

## 6. 结论与改进方向

本项目完成了一个 C++17 实现的 SQLite 自然语言查询工具：提前读取真实结构，生成只读 SQL，并以有限执行反馈形成纠错闭环。基础语法、分模块源码、JSON 配置和内置数据降低了阅读与复现成本；CSV 保留了简单的模式对照能力。

后续重点是在更大、更多数据库的 dev 样本上重复评测，分析字段匹配、JOIN、聚合和 Top-N 错误，区分语法修正与语义正确性。连续输入目前是独立问题，涉及“继续筛选”等上下文追问仍需单独设计。

**展示配套：** [7 页汇报提纲](presentation-outline.md)。<br>
**证据与细节：** [样例 CSV](evidence/sample.csv)、[dev 首题 CSV](evidence/spider-dev-1.csv)、[验证记录](../verification/current.md)、[技术说明](../guides/design.md)。

**参考文献：**

1. Yu et al. *Spider: A Large-Scale Human-Labeled Dataset for Complex and Cross-Domain Semantic Parsing and Text-to-SQL Task*. EMNLP 2018. https://yale-lily.github.io/spider
2. SQLite 官方文档：https://www.sqlite.org/docs.html
3. cJSON 1.7.19：https://github.com/DaveGamble/cJSON/tree/v1.7.19
