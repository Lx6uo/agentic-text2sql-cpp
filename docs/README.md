# 文档导航

当前实现是 **C++17 + SQLite + cJSON + 固定 SQL 纠错**。按你的目的选择文档；汇报、代码说明和验证记录各自承担不同用途。

| 你想做什么 | 阅读材料 | 说明 |
|---|---|---|
| 启动程序、配置 API Key、运行评测 | [仓库 README](../README.md) | 实际使用入口，包含 JSON 配置、数据库和 Benchmark 命令 |
| 看懂项目整体，准备课程汇报 | [当前项目报告](report/project-report.md) · [PDF](report/project-report.pdf) | 同一份四页报告的两个格式，描述当前技术栈、流程、效果和评价边界 |
| 准备 PPT 或现场演示 | [七页汇报提纲](report/presentation-outline.md) | 每页讲什么、演示顺序，以及哪些结果可以作为当前结论 |
| 阅读基础 C++ 和指针写法 | [代码阅读说明](guides/basic-cpp.md) | 文件分工、结果结构体和动态数组释放 |
| 理解查询、执行限制和评测规则 | [技术设计](guides/design.md) | 固定纠错、SQLite 控制、JSON 配置、Windows 和 CSV 比较方法 |
| 核对当前验证结果 | [当前验证记录](verification/current.md) | 当前版本实际完成的检查及范围 |
| 查看报告的原始回归数据 | [报告证据](report/evidence/README.md) | 两份 CSV 和校验清单；用于追溯报告表格 |
| 追溯旧版本 | [历史验证归档](archive/verification-history.md) | 旧菜单、动态工具、旧测试和旧命令的历史记录，不作为当前使用说明 |

推荐顺序：先看仓库 README 使用程序，再读项目报告了解全貌；需要看代码时进入 guides，需要核对结论时查看 verification 和 evidence。

```text
docs/
├── README.md                  # 本导航
├── report/                    # 课程展示材料
│   ├── project-report.md
│   ├── project-report.pdf
│   ├── presentation-outline.md
│   ├── print.css              # PDF 排版样式，无需日常阅读
│   └── evidence/              # 原始 CSV、说明和校验清单
├── guides/                    # 当前代码与设计说明
│   ├── basic-cpp.md
│   └── design.md
├── verification/
│   └── current.md             # 当前验证
└── archive/
    └── verification-history.md
```

`report/evidence/manifest.json` 记录源码基准、模型名、分组统计和文件 SHA-256。报告中的小样本验证用于确认流程，不代表完整 Spider 准确率。旧版 20 题 Agent 实验与当前固定纠错实现应分别阅读。
