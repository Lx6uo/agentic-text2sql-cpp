# 固定依赖

- SQLite 3.50.4 (3500400): https://sqlite.org/2025/sqlite-amalgamation-3500400.zip 。公有领域：https://sqlite.org/copyright.html 。以 SQLITE_OMIT_LOAD_EXTENSION 和 SQLITE_DQS=0 编译。
- nlohmann/json 3.12.0: https://github.com/nlohmann/json/releases/tag/v3.12.0 。MIT，见 json/LICENSE.MIT。
- libcurl >= 7.68：由系统或包管理器提供并通过 CMake `find_package(CURL)` 链接，不复制源码到仓库。curl 使用 curl license（MIT/X derivative）：https://curl.se/docs/copyright.html 。Windows 推荐 vcpkg 的 `curl:x64-windows-static`。

源码随仓库提供；CMake 不访问网络。校验值见 SHA256SUMS。
