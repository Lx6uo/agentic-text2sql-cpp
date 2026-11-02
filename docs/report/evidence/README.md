# 当前报告的回归证据

> 用途：核对 [项目报告](../project-report.md) 的验证表格，不作为完整 Spider 准确率评测。

报告基准为本地源码提交 `9ae5440`，回归执行于 2026-11-02。使用当前 C++17、cJSON、固定纠错实现，通过 CMake 在源码目录外构建，使用现有模型配置调用真实 AI。

| 文件 | 数据 | 题目数 | CSV 行数 |
|---|---|---:|---:|
| [sample.csv](sample.csv) | 内置 company 样例库，2 道中文题 | 2 | 4 |
| [spider-dev-1.csv](spider-dev-1.csv) | 官方 Spider dev 第一题，concert_singer | 1 | 2 |

每题分别运行 `baseline`（一次执行）和 `corrected`（最多三次执行）。两组 CSV 中的执行及结果一致标记均为 1，错误为空。所有标准 SQL 只用于本地结果比较，没有发送给模型。

这两组数据用于功能回归，不合并为泛化准确率；不代表完整 dev 评测，也不能证明纠错优于 Baseline。报告不复用旧动态 Tool Calling 版本的 20 题实验指标。

文件只记录公开样例/数据集的问题、数据库标识、预测 SQL 和评价结果，不包含 API Key、配置内容或本地用户路径。

[manifest.json](manifest.json) 保存模型名、源码基准、分组统计和 Markdown/PDF/CSV 的 SHA-256，便于核对展示材料与原始证据。
