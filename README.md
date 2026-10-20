# Agentic Text-to-SQL

轻量 C++20 命令行项目：通过 DeepSeek Flash 工具调用探索 SQLite Schema，生成和只读执行 SQL，并有限修正错误。支持无需网络和密钥的 Mock Demo，以及 Spider 1.0 本地评价。

构建：`cmake -S . -B build && cmake --build build`。测试：`ctest --test-dir build --output-on-failure`。

项目正在分阶段实现。第三方源码固定在仓库中，构建不下载依赖。
