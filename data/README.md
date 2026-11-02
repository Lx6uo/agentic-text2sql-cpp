# 随仓库提供的数据

clone 后无需再下载数据库或数据集。样例 SQLite 直接随仓库提供，Spider 运行数据压缩为普通 Git 文件，CMake 配置时自动解压到构建目录的 `data/spider/`，不需要 Git LFS、Python 或网络下载。

| 内容 | 用途 |
|---|---|
| `company.sqlite` | 默认自然语言查询数据库，3 个部门、6 名员工 |
| `sample-spider/` | 2 道中文题和配套样例库，便于快速验证 CSV 评测 |
| `spider.zip` | Spider 完整运行数据：206 个不重复数据库及所有题目、表元信息与标准 SQL |
| `spider-manifest.json` | 原始文件大小、SHA-256 和题目数量 |
| `SHA256SUMS` | 随仓库数据文件的校验值 |

压缩包为 59,773,171 字节（约 57 MiB），小于 GitHub 普通文件大小上限。解压后约 515 MiB，构建时会占用相应磁盘空间。数据包未包含原下载包中重复的数据库副本、数据库 SQL 导出、原始 CSV 等转换材料；数据库内容、题目 JSON、元信息与标准答案文件保留原始字节。

## Spider 的范围和来源

来源：[Spider 官方页面](https://yale-lily.github.io/spider) 和原下载包。原始 `README.txt`、引用说明和数据源 README 保存在压缩包内。

| 划分 | 题目数 | 数据库数 |
|---|---:|---:|
| `dev.json` | 1034 | 20 |
| `train_spider.json` | 7000 | 140 |
| `train_others.json` | 1659 | 6 |
| `test.json` | 2147 | 40 |

原包 `database/` 的 166 个库与 `test_database/` 中共享的库逐一核对，内容相同；40 个额外测试库合并到压缩包的统一 `database/` 下，总计 206 个。所有题目的 `db_id` 都有对应 SQLite 文件。

运行评测：`text2sql benchmark`，数据集路径留空使用完整 Spider；想快速体验，输入构建目录下 `data/sample-spider`，题数填 2。AI 查询和评测需要使用者自己的 API Key，离线 Demo 无需配置。

数据的来源说明和使用引用请遵循压缩包内上游 README；项目 MIT 许可证不替代上游数据说明。
