# MemoryStore — 通用内存 KV

> `memory_store/memory_store.{hpp,cpp}` · 库 `core_memory_store` · 依赖 `Threads`

通用键值缓存（`string → Variant`），供运行时与模块存放配置缓存、插件状态等。
**与 TagEngine 区分**：无时间戳语义、无变更回调，是纯 KV，不是实时变量体系。

## 公开 API

```cpp
void set(std::string_view key, Variant value);              // 覆盖
std::optional<Variant> get(std::string_view key) const;
template<class T> std::optional<T> getAs(std::string_view key) const;  // 类型精确匹配，否则 nullopt
bool exists(std::string_view key) const;
bool erase(std::string_view key);
std::size_t size() const;
std::vector<std::string> keys() const;
void clear();
```

## 设计要点

- 与 TagEngine 同构：固定 `SHARD_COUNT = 16`，每片 `shared_mutex` + `unordered_map<string,Variant>`，
  `hash(key) & 15` 选片。读共享、写独占。
- `getAs<T>` 用 `std::get_if<T>` 做**精确类型**提取：键缺失或存的不是 `T` 都返回 `nullopt`
  （不做数值隐式转换，如存 `int32` 用 `getAs<int64_t>` 会失败）。

## 线程与异常语义

- 全部方法线程安全。`size/keys/clear` 逐片加锁（非全局快照原子，存在跨片的弱一致）。

## 待改善

| 项 | 说明 | 方向 |
|----|------|------|
| **getAs 不做数值转换** | 类型不完全一致即 `nullopt`。 | 视需要提供数值收窄/拓宽的便捷读取。 |
| **缺「类 Redis」语义** | 无 TTL/过期、无原子自增、无 watch/订阅。 | 若定位为缓存层可逐步补；当前仅最基础 KV。 |
| **clear/keys 全扫描** | 跨 16 片逐一加锁。 | 规模可控，暂不优化；超大键空间再议。 |
| **用途边界待明确** | 目前运行时内部尚无强依赖此模块的路径。 | 明确它服务于谁（配置缓存？插件状态？），否则有沦为「万能袋」的风险。 |
