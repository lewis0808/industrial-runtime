# TagEngine — 分片并发 Tag 存储

> `tag_engine/tag_engine.{hpp,cpp}` · 库 `core_tag_engine` · 依赖 `Threads`

运行时**唯一的实时变量存储**。面向「高频写 + 高频读」的工业采集场景。

## 公开 API

```cpp
bool write(const TagValue&);              // 返回是否相对旧值发生变化；变化则触发回调
bool write(TagValue&&);
std::size_t writeBatch(const std::vector<TagValue>&);  // 返回变化条数
std::optional<TagValue> read(std::string_view) const;
bool exists(std::string_view) const;
bool remove(std::string_view);            // 返回是否确实删除
std::size_t size() const;
std::vector<TagValue> snapshot() const;   // 逐片加锁拷贝
void setChangeCallback(ChangeCallback);   // ChangeCallback = void(const TagValue&)
```

## 设计要点

- **分片（sharding）**：固定 `SHARD_COUNT = 16` 个 shard，每片一把 `std::shared_mutex` +
  一个 `unordered_map<string,TagValue>`。`shardFor(name)` 用 `hash(name) & (16-1)` 选片。
  ⇒ 不同片的读写互不阻塞，写并发度随片数提升。
- **读写锁**：`read/exists/size/snapshot` 走 `shared_lock`（共享读），`write/remove` 走
  `unique_lock`（独占写）。
- **变更语义**：`write` 比较新旧 `value`（`Variant::operator==`，类型+值都相等才算未变），
  **仅在值变化时**返回 `true` 并触发回调。新增 Tag 视为变化。
- **右值写入回调取值**：`write(TagValue&&)` 把 tag move 进 map 后，内容已被搬空，故先存 `name`，
  回调前再 `read(name)` 取回存储值传给回调。
- **回调注册**：`setChangeCallback` 用独立的 `callbackMutex_`（`shared_mutex`）保护；
  触发时持 `shared_lock`，**在调用方（通常是插件）线程内同步执行**回调。

## 线程与异常语义

- 全部公开方法线程安全。回调在**写入者线程**同步触发——IRSP server 注册的 `routeTag`
  即跑在推送数据的插件线程上。
- 回调若抛异常会沿写入路径传播：core 不吞，**调用方须保证回调异常安全**。

## 待改善

| 项 | 说明 | 影响 |
|----|------|------|
| **单回调** | `setChangeCallback` 只保留最后一次注册的回调（注释明说）。 | 多消费者（IRSP + 录历史 + 规则引擎…）无法共存 → 需改多订阅注册表，返回订阅 id。 |
| **remove 不通知** | 删除 Tag 不触发任何回调。 | 订阅方（IRSP）无法感知 Tag 消失 → 需增加 删除/失效 事件。 |
| **同步回调阻塞推送** | 慢订阅者在写入者线程同步执行，拖慢推送插件。 | 高频场景需解耦（投递队列 / 专用分发线程）。 |
| **右值路径二次加锁** | `write(&&)` 触发回调时 `read(name)` 再锁一次该片。 | 可在 `writeImpl` 内一次性取回存储值，省一次加锁。 |
| **lookup 有分配** | `read/exists/remove` 用 `std::string(name)` 查 map。 | C++20 透明哈希（heterogeneous lookup）可省去临时 string 分配。 |
| **时间戳更新不传播** | 同值新时间戳判为「未变」，不回调。 | 心跳/保活刷新无法通知订阅方（按需可加「强制通知」选项）。 |
| **分片数固定 16** | 不可配置。 | 超大规模/超高并发或需调参。 |
| **无遍历/前缀查询** | 只有全量 `snapshot()`。 | IRSP 的 `SCAN`/Topic 子树匹配目前在 irsp 侧自行实现；core 无索引支持。 |
