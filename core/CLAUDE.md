## 项目目标

Core Runtime 是工业数据采集中间件的运行时核心。

## 目录
```markdown
core/
├── runtime_engine/        # Runtime主循环
├── tag_engine/            # Tag内存模型（核心）
├── event_bus/             # 高性能事件总线
├── scheduler/             # 任务调度（采集周期）
├── memory_store/          # 内存KV（类似Redis核心）
├── config/               # 配置系统
├── logger/
└── main.cpp
```
