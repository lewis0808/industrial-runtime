# IRP 会话示例（V1 / WebSocket / resp1）

每个代码块为一个 WS 二进制消息载荷（resp1 编码）。`C→S` 客户端发，`S→C` 服务端发。
为可读，二进制值以 `<...>` 注记，CRLF 省略显示但实际存在。

## 1. 握手（HELLO 强制）

```
C→S   *2  $5 HELLO  $1 1
S→C   %5  $6 server $24 industrial-runtime/1.0.0
            $7 version $1 1
            $8 encoding $5 resp1
            $10 transports $9 websocket
            $12 capabilities $9 tag,event
```

未握手即发命令：
```
C→S   *2  $3 GET  $4 a/b/c
S→C   -NOT_READY HELLO required first
```

## 2. 读取单个 Tag

```
C→S   *2  $3 GET  $26 factory1/line1/robot1/temp
S→C   %4  $4 name  $26 factory1/line1/robot1/temp
            $4 type  $3 f64
            $2 ts    :1749800000000000000
            $5 value $8 <8 字节 f64 小端 = 36.6>
```

不存在：
```
C→S   *2  $3 GET  $7 no/such
S→C   $-1
```

## 3. 游标遍历（SCAN，替代 KEYS）

```
C→S   *5  $4 SCAN  $1 0  $12 factory1/#  $5 COUNT  $3 100
S→C   *2  $3 a1f   *2  $26 factory1/line1/robot1/temp  $25 factory1/line1/robot1/load
C→S   *3  $4 SCAN  $3 a1f  $12 factory1/#
S→C   *2  $1 0     *1  $24 factory1/line2/conv1/spd
```
`nextCursor=0` 表示结束。

## 4. 单点关注（WATCH）与子树订阅（SUBSCRIBE）

```
C→S   *2  $5 WATCH  $26 factory1/line1/robot1/temp
S→C   :1

C→S   *2  $9 SUBSCRIBE  $12 factory1/#
S→C   :1
```

随后服务端在该连接主动推送（值变化时）：
```
S→C   %5  $4 push $3 tag
            $4 name $26 factory1/line1/robot1/temp
            $4 type $3 f64  $2 ts :... $5 value $8 <...>
```

## 5. 事件订阅

```
C→S   *3  $8 SUBEVENT  $7 warning  $5 alarm
S→C   :1
S→C   %6  $4 push $5 event
            $6 source $2 s7  $8 category $5 alarm
            $8 severity $7 warning  $2 ts :...  $7 message $13 high pressure
```

## 6. Stream（V1 预留）

```
C→S   *2  $9 SUBSTREAM  $20 factory1/cam1/frames
S→C   -NOT_IMPLEMENTED stream subscription is reserved for V2
```

## 7. 关闭

```
C→S   *1  $3 BYE
S→C   +OK
```
（随后双方走 WS Close 握手；server 清理本连接的 WATCH/SUBSCRIBE 状态。）
