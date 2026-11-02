# 当前版本验证记录

> 用途：记录当前版本已经实际验证的内容。旧版本记录已移至 [历史归档](../archive/verification-history.md)；报告的原始 CSV 和校验清单见 [报告证据](../report/evidence/README.md)。

## 当前展示材料与报告回归（2026-11-02）

[当前报告](../report/project-report.md)、[四页 PDF](../report/project-report.pdf) 和 [汇报提纲](../report/presentation-outline.md) 已围绕源码基准 `9ae5440` 重新整理，技术栈为 C++17、SQLite、cJSON 和 libcurl，流程为读取结构后生成 SQL、最多三次执行与纠错。报告中的 20 题动态 Agent 指标已移出当前性能结论。

本次在源码目录外构建当前程序，核对离线 Demo 的 SQL/结果分区与两次尝试，并重新执行两题内置中文样例和官方 Spider dev 第一题。Baseline 与固定纠错两种模式在两组中均执行成功并匹配标准结果。原始 CSV、模型名、源码基准和文件 SHA-256 保存在 `docs/report/evidence/`。

PDF 已逐页视觉检查，共四页，无孤立末行页；正文、标题和链接均对应当前版本，没有旧版 C++20/nlohmann 技术栈、65%/75% 实验结论或本地用户路径。上述样本仅用于功能回归，不代表完整 Spider 准确率。

## 当前版本：随 clone 提供数据库和数据集

仓库加入默认样例 SQLite、两题中文评测数据和 59,773,171 字节的 Spider ZIP。压缩包保留 206 个不重复数据库、dev 1034 题、train_spider 7000 题、train_others 1659 题和 test 2147 题，以及元信息和标准 SQL。CMake 自动解压到构建目录，不需要 Git LFS、Python 或额外下载数据。

实际验证：

- 所有归档文件与来源 SHA-256 一致，206 个 SQLite 文件均以只读方式打开成功，全部题目 db_id 都有对应数据库。
- 新规则放行内置数据，继续忽略本地配置、用户数据库、SQLite WAL/SHM、解压数据和结果。新增 Git 文件均小于 100 MiB。
- 使用当前完整源码和数据建立临时 Git 提交，再执行干净本地 clone。克隆后 C++17 构建成功，不配置 API Key 也能自动连接样例库并通过 `/demo` 离线体验。
- Benchmark 数据路径留空使用内置完整 Spider。官方 dev 第一题真实 AI 对照中，Baseline 与纠错模式均执行成功并匹配标准结果，CSV 两行完整。
- 安装后移动整个安装目录，程序仍找到样例数据库和完整 Spider，离线 Demo 成功；源构建目录的位置不影响数据定位。
- 未变化的归档不会重复解压。验证在 macOS 完成，Windows 多配置与安装目录适配保留。
