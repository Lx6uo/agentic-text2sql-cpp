# SQLite 自然语言查询与结果分析

使用 C++17、SQLite、cJSON 和 libcurl，将自然语言问题转换成只读 SQL。程序读取真实表结构，生成并执行一次 SQL，先显示 SQL 和查询结果，再请求模型分析结果并显示分析文字。评测结果直接记录在文档中。

## 文档入口

项目文档统一保留三份，按用途阅读：

| 文档 | 用途 |
|---|---|
| [README](README.md) | 构建环境、配置 API Key、启动查询和数据使用 |
| [项目设计与技术文档](docs/design-and-implementation.md) | 流程、模块、基础 C++ 与指针、API 协议、数据库控制和评测记录方式 |
| [汇报文档](docs/project-report.md) | 数据集背景、项目目标、实现效果、评测记录和现场汇报提纲 |

## 构建和启动

仓库直接包含默认数据库 [college_2.sqlite](data/college_2.sqlite)，约 2 MiB，有 11 张表、2000 名学生、50 名教师和 200 门课程。Clone 后构建即可启动默认查询，无需下载或解压数据。完整 Spider ZIP 仍随仓库提供，构建过程只复制现成的 SQLite 文件和小型样例，不读取 ZIP、不自动解压或校验归档。

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

SQL 执行失败会显示统一错误，随后可以继续提问；分析失败仍显示已有 SQL 和查询结果。默认最多读取 100 行、约 1 MiB 结果，分析默认接收最多 100 行、128 KiB 的结构化数据；单值最多 2048 字节，截断时会注明实际发送行数和完整性。详情类问题会整理主体信息、课程与成绩等明细。跨表分别列出原始记录时，模型统一返回“表名”和“JSON 记录”两列，保留各表字段；行数上限适用于整条 SQL。

EOF 正常退出，Ctrl+C 取消请求或 SQL，退出码为 130。

## 评测记录与其他数据库

项目已移除评测命令、统计、CSV 导出和结果比较工具。已有两题样例的历史结果、大学数据库的查询结果及评价范围保存在[汇报文档](docs/project-report.md#44-功能验证与评价范围)。新增评测采用人工核对，并在文档记录问题、数据库、SQL、预期与实际结果、日期、版本和模型；方法见[技术文档](docs/design-and-implementation.md#7-文档评测记录)。

默认大学库和 company 小型库已经是可直接打开的 SQLite 文件，例如：

```bash
./build/text2sql --db data/company.sqlite
```

完整题目、数据库与标准 SQL 仍保存在 `data/spider.zip`。需要其中其他数据库时，手动解压到一个目录，再通过 `--db` 指向具体 SQLite 文件；Windows 可在资源管理器中使用“全部提取”，例如在项目根目录解压后：

```powershell
.\build\Release\text2sql.exe --db ".\data\spider\database\concert_singer\concert_singer.sqlite"
```

标准 SQL 用于本地人工核对，不放入模型问题。普通自然语言查询仍会请求结果分析。

## 数据、源码和许可证

`data/college_2.sqlite` 是默认大学数据库，`data/company.sqlite` 和 `data/sample-spider/` 是小型回归样例，`data/spider.zip` 是完整数据归档。所有仓库数据原样保留，来源与范围在[技术文档](docs/design-and-implementation.md#8-仓库数据与来源)中统一说明。原有 [data/SHA256SUMS](data/SHA256SUMS) 和 manifest 作为数据来源记录保留，构建和运行不执行这些校验。

功能分别放在 `src/` 的多个 CPP 中，由 `main.cpp` 按顺序包含，CMake 只编译入口。代码使用普通函数、结构体、字符串、数组、循环和简单指针；资源释放与阅读顺序见[技术文档](docs/design-and-implementation.md#3-代码模块与基础-c-写法)。

复制程序时同时复制构建目录的 `data/`；Windows 的 `Release/` 程序也可与 `data/` 放在同一目录。项目不提供 CMake 安装目标。构建文件、本地数据库、手动解压的数据及本地配置由 `.gitignore` 排除。

项目采用 [MIT 许可证](LICENSE)。SQLite 为公有领域，cJSON 使用 [MIT](third_party/cjson/LICENSE)，libcurl 使用 [curl license](https://curl.se/docs/copyright.html)。固定依赖来源及校验说明见[技术文档](docs/design-and-implementation.md#9-依赖跨平台与构建)。上游数据说明保留在 Spider 压缩包内。
