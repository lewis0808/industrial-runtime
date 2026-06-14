# Config — JSON + 点号路径配置

> `config/config.{hpp,cpp}` · 库 `core_config` · 依赖 `nlohmann_json`

基于 nlohmann/json 的运行时配置，支持点号分隔的层级键访问。**不抛异常**：失败一律返回
默认值 / `nullopt` / `false`。

## 公开 API

```cpp
bool loadFile(const std::string& path);    // 失败返回 false（不抛）
bool loadString(const std::string& text);
template<class T> T get(std::string_view dottedKey, T defaultValue) const;       // 缺失/类型不符→default
template<class T> std::optional<T> tryGet(std::string_view dottedKey) const;     // 缺失/类型不符→nullopt
bool has(std::string_view dottedKey) const;
nlohmann::json snapshot() const;           // 整份配置副本
```

示例：`cfg.get<int>("irsp.port", 9777)` / `cfg.get<std::string>("logger.level", "info")`。

## 设计要点

- **并发**：`shared_mutex`——读（`get/tryGet/has/snapshot`）共享锁，加载（`loadFile/loadString`）独占锁，
  整份 `root_` 原子替换。读期间不会读到半加载状态。
- **点号解析** `resolve`：从 `root_` 起按 `.` 逐段下钻 `find`，任一段非对象或缺失即 `nullptr`。
- **容错解析**：`json::parse(in, nullptr, true, true)` —— 第 3 参 `allow_exceptions=true`（内部捕获），
  第 4 参 `ignore_comments=true`（**允许 JSON 带注释**）。解析异常被 `catch` 转为返回 false。
- **键路径约定**：配置用 `.` 分层（与 **Tag/Topic 用 `/`** 互不相干，禁止互转）。

## 待改善

| 项 | 说明 | 方向 |
|----|------|------|
| **只读，无运行时写** | 加载后无 `set`/修改 API。 | 如需运行时下发配置项，补线程安全的 set + 变更通知。 |
| **点号键无法寻址含「.」的键** | `.` 恒被当分隔符，JSON 里名为 `"a.b"` 的键取不到。 | 当前约定下可接受；如遇到需提供转义或数组式 key。 |
| **不支持数组索引** | `resolve` 只下钻 object，无法 `a.list.0` 取数组元素。 | 按需补数字段索引解析。 |
| **无校验/无热重载/无 env 覆盖** | 无 schema 校验、无文件监听、无环境变量覆盖。 | 视部署需求逐步补（热重载对工业现场有价值）。 |
