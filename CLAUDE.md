# 工业中间件


# 项目定位
工业数据运行时 (Industrial Runtime)
 
我们做的不是Kepware，不是Ignition，不是SCADA，我们做的是工业基础设施（Industrial Middleware），用户不是工厂客户，而是开发者。打破设备
与IT壁垒、统一数据模型（Unified Data Model）、协议驱动生态（Protocol-Driven Ecosystem）、高性能事件总线（High-Performance Event Bus）。

核心目标：

- 接入工业设备
- 标准化工业数据
- 提供统一数据总线
- 提供事件总线
- 提供流数据处理能力
- 提供多语言 SDK
- 提供插件生态

非目标：

- HMI
- SCADA
- MES
- ERP
- 报表系统
- 业务流程引擎

## 项目目录
```markdown
industrial-runtime/
├── core/                # runtime kernel
├── irp/                 # IRP protocol (tag system)
├── sdk/
│   ├── irp-client/      # 开发语言 SDK（Python/Java/JS）
│   ├── plugin-sdk/      # 插件开发 SDK（设备侧）
│
├── plugins/             # 动态加载插件
│   ├── camera/
│   ├── radar/
│   ├── s7/
│
├── stream/              # high bandwidth data
├── drivers/             # legacy PLC drivers (可选)
└── tools/
```

## 架构原则

### 原则一

Core 不依赖设备。

禁止：
```
Core -> OPCUA
Core -> S7
Core -> Camera SDK
Core -> Radar SDK
```

允许：
```markdown
Device Plugin -> Runtime API -> Core
```
### 原则二
所有设备必须以插件形式接入。

例如：
```markdown
S7
Modbus
OPC UA
ABB
Basler
海康
Velodyne
```
均属于 Plugin。

---
## 数据模型规范
### Tag 数据

用于：
```markdown
PLC变量
状态量
报警量
数值量

标准结构：

struct TagValue
{
std::string name;
DataType type;
Timestamp timestamp;
Variant value;
};

```
禁止：
```markdown
void*
```
Event 数据

用于：
```markdown
报警
状态变化
系统通知
```

禁止用于存储实时变量。

### Stream 数据

用于：
```markdown
图像
视频
点云
雷达数据
二进制流
```
支持类型：
```markdown
Frame
PointCloud
BinaryBlob

```
禁止：
```markdown
图像放入Tag
点云转JSON
```
---

## 插件规范
所有插件必须实现：
```markdown
class IPlugin
{
public:

    virtual bool init() = 0;

    virtual bool start() = 0;

    virtual bool stop() = 0;

    virtual bool destroy() = 0;
};
```
导出接口：
```markdown
extern "C"
{
    PluginInfo getPluginInfo();

    IPlugin* createPlugin();
}
```
禁止：
```markdown
导出C++类
跨DLL传递STL对象
跨DLL传递异常
```
---

## Runtime API
插件仅允许调用：
```markdown
pushTag()

pushEvent()

pushStream()
```
禁止：
```markdown
访问TagEngine内部
访问MemoryStore内部
访问EventBus内部
```
---

## 内存管理规范
优先：
```markdown
std::unique_ptr
```
其次：
```markdown
std::shared_ptr
```
禁止：
```markdown
new
delete
```
除非必须与第三方SDK交互。

---

## 线程规范
优先：
```markdown
std::jthread
```
所有循环必须支持：
```markdown
std::stop_token
```
禁止：
```markdown
detach()
无限循环且无法退出
```
---
## 命名规范
### 类名：
```markdown
TagEngine
PluginManager
RuntimeServer
```
PascalCase
### 函数名：
```markdown
start()
stop()
subscribe()
publish()
```
camelCase

### 变量名：

```markdown
tagName
pluginId
bufferSize
```
camelCase

### 常量：
```markdown
MAX_TAG_COUNT
DEFAULT_BUFFER_SIZE
```
UPPER_CASE

### Tag 命名 / Topic：

```markdown
Tag Name == Topic
Topic Separator == /
```

- Tag 名即 IRP Topic，统一使用 `/` 分层，例如：
```markdown
example/temperature
factory1/line1/robot1/temp
```
- 禁止使用 `.` 作为层级分隔（`.` 仅用于 config 配置路径，与 Tag 无关）。
- 禁止做 `.` ↔ `/` 转换层（永久技术债）。
- 订阅通配采用 MQTT 风格：`+`（单层）、`#`（多层，末段）。

---
## AI 生成代码规则

生成代码时必须遵守：
- 优先使用 C++20。
- 优先使用 STL。
- 优先组合而非继承。
- 模块之间保持低耦合。
- Core 不得依赖具体设备。
- 保持 ABI 稳定。
- 生成生产级代码。
- 必须考虑线程安全。
- 必须考虑异常安全。
- 必须考虑跨平台兼容。
- 提供必要的单元测试。

---
## 项目最高原则
```markdown
Core 不依赖设备
设备全部插件化
应用全部通过 IRP 接入
Runtime 是唯一数据中心
Tag 与 Stream 是两套独立体系
```
