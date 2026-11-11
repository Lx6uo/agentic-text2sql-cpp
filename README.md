# SQLite 自然语言查询与结果分析

用自然语言查询 SQLite 数据库，并由模型解释查询结果。主要流程：读取表结构 → 生成 SQL → 执行只读查询 → 显示结果 → 模型分析。

## 环境依赖

需要 C++17 编译器、CMake ≥ 3.20 和 libcurl ≥ 7.68。SQLite 与 cJSON 源码已随项目提供。

macOS 使用 Xcode Command Line Tools，通过 `brew install cmake` 安装 CMake。Ubuntu/Debian：

```bash
sudo apt-get install cmake g++ libcurl4-openssl-dev
```

## 配置

将 [config.example.json](config.example.json) 复制为 `config.json`，填写 API Key；模型和服务地址按使用的服务设置。

```json
{
  "api": {
    "base_url": "https://api.deepseek.com",
    "model": "deepseek-flash",
    "api_key": "填写你的 API Key"
  },
  "database": {
    "path": "data/college_2.sqlite"
  }
}
```

也可用 `DEEPSEEK_API_KEY` 环境变量设置密钥，优先于配置文件。数据库路径优先级为 `--db` → `database.path` → 内置大学库；配置中的相对路径以配置文件所在目录为基准。

## 构建与启动

在项目目录执行：

```bash
cmake -S . -B build
cmake --build build -j 4
./build/text2sql
```

指定配置或数据库：

```bash
./build/text2sql --config config.json --db data/company.sqlite
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

## 使用

启动后输入问题，例如：

```text
查询学生总人数
查询每个院系的学生人数，按人数从多到少排列
列出选课记录数量最多的5门课程，显示课程名称和选课记录数
```

`/help` 查看帮助，`/exit` 退出，Ctrl+C 取消。每次提问独立处理，依次显示 SQL、查询结果和分析。

终端和分析共用最多 2000 行、128 列、1 MiB 的结果，单值最多 8 KiB；超限时显示截断标记。使用其他 Spider 数据库时，解压 `data/spider.zip`，通过 `--db` 指定 SQLite 文件。复制程序时同时带上构建目录中的 `data/`。

## 项目文件

| 文件或目录 | 用途 |
|---|---|
| `src/main.cpp` | 入口，按顺序包含各功能 CPP |
| `src/agent.cpp`、`src/cli.cpp` | 查询与分析流程、终端交互 |
| `src/database.cpp` | 表结构读取、SQL 执行和结果存储 |
| `src/llm.cpp`、`src/json.cpp` | 模型请求与 JSON 处理 |
| `src/common.cpp`、`src/config.cpp`、`src/files.cpp` | 共用结构与上限、配置和路径处理 |
| `data/` | 默认大学库、company 样例和 Spider 数据归档 |
| `third_party/` | SQLite、cJSON 源码 |
| `CMakeLists.txt` | 构建配置 |
| [技术文档](docs/design-and-implementation.md) | 设计与实现说明 |
| [汇报文档](docs/project-report.md) | 项目介绍与验证记录 |

项目采用 [MIT 许可证](LICENSE)。
