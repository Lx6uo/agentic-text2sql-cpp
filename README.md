# SQLite 自然语言查询与结果分析

使用 C++17、SQLite、cJSON 和 libcurl，将自然语言问题转换成只读 SQL。程序读取真实表结构，生成并执行一次 SQL，成功后再请求模型分析结果；终端分别显示 SQL、查询结果和分析文字。Benchmark 通过独立命令手动运行。

## 文档入口

项目文档统一保留三份，按用途阅读：

| 文档 | 用途 |
|---|---|
| [README](README.md) | 安装、配置 API Key、启动查询和手动评测 |
| [项目设计与技术文档](docs/design-and-implementation.md) | 流程、模块、基础 C++ 与指针、API 协议、数据库控制和评测规则 |
| [汇报文档](docs/project-report.md) | 数据集背景、项目目标、实现效果、验证结果和现场汇报提纲 |

## 构建和启动

仓库直接包含默认数据库 [college_2.sqlite](data/college_2.sqlite)，约 2 MiB，有 11 张表、2000 名学生、50 名教师和 200 门课程。完整 Spider 运行数据也随仓库提供，编译时自动解压，无需另行下载数据或使用 Git LFS。

需要 C++17 编译器、CMake ≥ 3.20 和 libcurl ≥ 7.68。SQLite 3.50.4、cJSON 1.7.19 的固定源码已经包含在仓库中。

```bash
cmake -S . -B build
cmake --build build -j 4
./build/text2sql
```

macOS 使用 Xcode Command Line Tools 提供的编译器和 libcurl；CMake 可通过 Homebrew 安装。Ubuntu/Debian 安装依赖：

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
```

构建后直接运行会连接 `college_2` 并显示自然语言输入提示。运行查询需要配置自己的 API Key。

## 配置 API Key 和数据库

复制 [配置示例](config.example.json)，在 `api.api_key` 中填写密钥；`api.model` 和 `api.base_url` 可按模型服务修改。

```bash
cp config.example.json config.json
```

默认模型为 `deepseek-flash`，地址为 `https://api.deepseek.com`。默认读取当前目录的可选 `config.json`；通过 `--config` 指定的文件必须存在且为有效配置：

```bash
./build/text2sql --config "配置/我的配置.json"
```

也可使用 `DEEPSEEK_API_KEY` 环境变量。环境变量优先于配置文件，空环境变量也会覆盖文件值。macOS/zsh 可这样输入，密钥不会回显或进入命令历史：

```bash
read -rs DEEPSEEK_API_KEY
export DEEPSEEK_API_KEY
```

数据库路径的优先级为：`--db` 参数 → JSON 中的 `database.path` → 程序附带的 `college_2.sqlite`。配置中的相对路径以配置文件所在目录为基准。

```bash
./build/text2sql --db "data/college_2.sqlite"
```

本地配置和环境文件由 `.gitignore` 排除；真实密钥不随仓库提供。各配置字段的默认值和作用见[技术文档](docs/design-and-implementation.md#5-json-配置与-api-请求)。

## 输入自然语言

启动后直接提问，每次查询后可继续输入。可以尝试：

```text
查询学生总人数
查询每个院系的学生人数，按人数从多到少排列
列出选课记录数量最多的5门课程，显示课程名称和选课记录数
```

`/help` 显示引导，`/exit` 退出。单独输入 `help`、`exit`、问候或其他无查询意图的内容时，模型按系统提示词返回固定标记，程序捕获后显示统一提示。含有 help/exit 的有效数据查询仍可执行。

成功查询通常调用两次 API：第一次生成 SQL，第二次分析结果。每个问题重新建立上下文；连续输入适合提出独立问题。

SQL 执行失败会显示统一错误，随后可以继续提问；分析失败仍显示已有 SQL 和查询结果。默认最多读取 100 行、约 1 MiB 结果，发送给分析模型的预览最多 10 行，截断会标记。跨表分别列出原始记录时，模型统一返回“表名”和“JSON 记录”两列，保留各表字段；行数上限适用于整条 SQL。

EOF 正常退出，Ctrl+C 取消请求或 SQL，退出码为 130。

## 手动运行 Benchmark

```bash
./build/text2sql benchmark
# 使用指定配置
./build/text2sql --config config.json benchmark
```

依次输入数据集目录、划分、题数和 CSV 路径。目录留空使用完整 Spider，划分默认 `dev`，题数默认 20，CSV 路径留空自动生成。支持 `dev`、`train_spider`、`train_others`、`test`，`train` 是 `train_spider` 的别名。

快速验证可填写 `build/data/sample-spider`、`dev`、`2` 和一个新的 CSV 路径。每题只生成并执行一次预测 SQL，再与本地标准 SQL 的结果比较；评测不调用结果分析 API。CSV 字段为：

```text
question,db_id,sql,executed,matched,error
```

已有输出文件不会覆盖。终端汇总实际尝试题数、执行成功率和本地结果一致率；比较口径见[技术文档](docs/design-and-implementation.md#7-手动-benchmark-与结果比较)。启动查询程序时不会自动运行评测。

## 数据、源码和许可证

`data/college_2.sqlite` 是默认大学数据库，`data/company.sqlite` 和 `data/sample-spider/` 是小型回归样例，`data/spider.zip` 是完整运行包。来源与范围在[技术文档](docs/design-and-implementation.md#8-数据打包与来源)中统一说明，校验值见 [data/SHA256SUMS](data/SHA256SUMS)。

功能分别放在 `src/` 的多个 CPP 中，由 `main.cpp` 按顺序包含，CMake 只编译入口。代码使用普通函数、结构体、字符串、数组、循环和简单指针；资源释放与阅读顺序见[技术文档](docs/design-and-implementation.md#3-代码模块与基础-c-写法)。

复制程序时同时复制旁边的 `data/`；安装时程序和数据会一并安装。构建文件、本地数据库和评测结果由 `.gitignore` 排除。

项目采用 [MIT 许可证](LICENSE)。SQLite 为公有领域，cJSON 使用 [MIT](third_party/cjson/LICENSE)，libcurl 使用 [curl license](https://curl.se/docs/copyright.html)。固定依赖来源及校验说明见[技术文档](docs/design-and-implementation.md#9-依赖跨平台与构建)。上游数据说明保留在 Spider 压缩包内。
