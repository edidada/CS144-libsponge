# CS144 Lab 0～6：代码修改与知识点总结

本文按 Lab 总结本仓库实际完成了哪些代码、各模块之间如何衔接，以及每个阶段对应的网络与 C++ 知识点。

代码范围通过比较每个 starter 合入点和对应的 `Pass: LAB_N` 提交得到。个别基础模块在后续 Lab 中又被重构，因此“当前代码”可能与最初通过该 Lab 时的数据结构不同，但职责没有变化。

## 总体脉络

整个项目自底向上实现了一套用户态 TCP/IP 协议栈：

```text
Lab 0  有界可靠字节流 + HTTP 客户端
  ↓
Lab 1  乱序子串重组为有序字节流
  ↓
Lab 2  TCP 序号换算 + TCP 接收端
  ↓
Lab 3  TCP 发送端 + 超时重传
  ↓
Lab 4  组合收发两端，形成完整 TCP 连接状态机
  ↓
Lab 5  IPv4 数据报与 Ethernet 帧之间的网络接口 + ARP
  ↓
Lab 6  IPv4 路由器 + 最长前缀匹配
```

| Lab | 核心模块 | 主要协议层 | 实现提交范围 |
|---|---|---|---|
| Lab 0 | `webget`、`ByteStream` | 应用层 / 流抽象 | `337e97d..760c911` |
| Lab 1 | `StreamReassembler` | TCP 接收数据重组基础 | `1fe5b58..131c535` |
| Lab 2 | `WrappingInt32`、`TCPReceiver` | TCP 接收端 | `61ba380..a8364df` |
| Lab 3 | `TCPSender` | TCP 发送端 | `0829cac..95b57c1` |
| Lab 4 | `TCPConnection` | 完整 TCP 连接 | `ee87ff2..b845b8c` |
| Lab 5 | `NetworkInterface` | Ethernet / ARP / IPv4 | `85d61bf..646d8cc` |
| Lab 6 | `Router` | IPv4 路由 | `707456c..888faff` |

## Lab 0：ByteStream 与 webget

### 修改的主要代码

- `apps/webget.cc`
- `libsponge/byte_stream.cc`
- `libsponge/byte_stream.hh`

实际改动约为 68 行新增、16 行删除。

### 实现内容

#### `webget`

`get_URL()` 完成一个最小 HTTP 客户端：

1. 用主机名和 `http` 服务构造 `Address`。
2. 通过 `FullStackSocket` 建立 TCP 连接。
3. 发送 HTTP/1.1 请求行和 `Host` 请求头。
4. 使用 `shutdown(SHUT_WR)` 关闭本地写方向，但继续接收响应。
5. 循环读取，直到 socket 到达 EOF，而不是只调用一次 `read()`。
6. 关闭并等待连接完全结束。

#### `ByteStream`

实现一个容量受限、先进先出的可靠字节流：

- `write()`：最多写入剩余容量允许的字节；
- `peek_output()`：查看但不消费输出；
- `pop_output()`：消费指定数量的字节；
- `read()`：组合 peek 和 pop；
- `end_input()`：声明输入不会再增加；
- `eof()`：输入已结束并且缓冲区已读空；
- `remaining_capacity()`、`buffer_size()`、`buffer_empty()`：维护容量状态；
- `bytes_written()`、`bytes_read()`：维护累计计数；
- `set_error()`/`error()`：传播流错误状态。

该模块后来先后使用过 `std::deque` 和 `BufferList` 重构，但对外仍保持相同的有限容量流语义。

### 对应知识点

- TCP socket 的连接、读写、半关闭和 EOF；
- HTTP/1.1 最小请求格式，特别是 `Host` 头和 `\r\n`；
- 流式 I/O 不保证一次 `read()` 得到完整响应；
- 有界缓冲区、背压和生产者/消费者模型；
- FIFO、容量不变量和累计计数；
- “输入结束”与“当前缓冲区为空”的区别；
- C++ 资源管理和异常处理。

### 验证命令

```bash
cmake --build cmake-build-debug-wsl --target check_lab0
```

## Lab 1：StreamReassembler

### 修改的主要代码

- `libsponge/stream_reassembler.cc`
- `libsponge/stream_reassembler.hh`
- 少量修正 `libsponge/byte_stream.cc`

这一阶段最初采用链表保存片段；Lab 2 开始前又重构为按起始位置排序的 `std::set<block_node>`。

### 实现内容

`StreamReassembler::push_substring(data, index, eof)` 将可能乱序、重复或重叠到达的子串还原为连续字节流：

- 用绝对 stream index 标识每个子串的位置；
- 丢弃已经组装过的前缀；
- 裁剪或拒绝超出接收容量窗口的数据；
- 将新片段与前后相邻或重叠片段合并；
- 对重复区域只计数一次，正确维护 `unassembled_bytes()`；
- 当最靠前的片段从 `_head_index` 开始时，将其写入 `ByteStream`；
- 只有收到 EOF 标志且 EOF 之前的所有字节都已组装时，才结束输出流。

核心状态包括：

- `_head_index`：下一个期待写入输出流的绝对位置；
- `_blocks`：尚未连续的有序片段集合；
- `_unassembled_byte`：已缓存但尚未写入输出流的字节数；
- `_capacity`：重组器与输出流共享的容量约束；
- `_eof_flag`：是否已经看到逻辑流末尾。

### 对应知识点

- TCP/IP 网络中乱序、重复、重叠数据的处理；
- 从不可靠分段输入恢复可靠有序字节流；
- 滑动接收窗口和容量约束；
- 区间合并、去重与边界裁剪算法；
- 有序集合、自定义比较器和迭代器失效；
- 维护数据结构不变量：已组装、未组装和空闲容量之和受总容量限制；
- EOF 是逻辑位置，不等于“某个带 EOF 标记的片段已经到达”。

### 验证命令

```bash
cmake --build cmake-build-debug-wsl --target check_lab1
```

## Lab 2：WrappingInt32 与 TCPReceiver

### 修改的主要代码

- `libsponge/wrapping_integers.cc`
- `libsponge/tcp_receiver.cc`
- `libsponge/tcp_receiver.hh`
- 重构 `libsponge/stream_reassembler.*`
- 重构 `libsponge/byte_stream.*`

### 实现内容

#### 32 位环绕序号

- `wrap(n, isn)`：把 64 位绝对序号加上 ISN 后映射到 32 位 TCP 序号空间；
- `unwrap(n, isn, checkpoint)`：在所有可能的 64 位候选值中，选择距离近期 checkpoint 最近的一个。

这里需要处理模 `2^32` 运算，以及 checkpoint 附近的前一圈、当前圈和后一圈候选值。

#### TCP 接收端

`TCPReceiver::segment_received()` 将 TCP segment 转换为重组器能理解的字节流片段：

- 首个有效 SYN 建立对端 ISN；
- SYN 之前到达的普通 segment 不被接收；
- 将 32 位 `seqno` 展开为绝对序号；
- 把 TCP sequence-space 序号转换为 stream index；
- 将 payload 交给 `StreamReassembler`；
- 处理重复 SYN、FIN、空 segment 和窗口边界；
- 生成累计 ACK；
- 根据输出缓冲区占用量计算通告窗口。

SYN 和 FIN 各占用一个 TCP sequence number，但不占用字节流中的数据位置，这是该 Lab 最重要的偏移关系之一。

### 对应知识点

- TCP 32 位环绕序号和 ISN；
- 模运算、无符号整数溢出和最近 checkpoint 反解；
- TCP sequence space 与应用字节流 index 的区别；
- SYN/FIN 为什么各消耗一个序号；
- 累计确认 ACK：ACK 表示下一个期待接收的序号；
- 接收窗口与流量控制；
- TCP segment 可接受性和窗口边界；
- 接收器、重组器和字节流的分层组合。

### 验证命令

```bash
cmake --build cmake-build-debug-wsl --target check_lab2
```

## Lab 3：TCPSender

### 修改的主要代码

- `libsponge/tcp_sender.cc`
- `libsponge/tcp_sender.hh`
- 少量调整 receiver、reassembler 和 byte stream 接口

主体实现约新增 106 行发送端逻辑。

### 实现内容

#### 填充发送窗口

`fill_window()` 根据对端通告窗口产生 segment：

- 首次主动发送时先发送 SYN；
- 从发送字节流读取数据，单个 segment 不超过 `TCPConfig::MAX_PAYLOAD_SIZE`；
- 不让 sequence-space 中未确认的数据超过接收窗口；
- 输入结束且窗口有空间时附带或单独发送 FIN；
- 对端通告零窗口时，把有效窗口暂视为 1，从而允许 zero-window probe。

#### ACK 与在途数据

`ack_received()`：

- 使用 `unwrap()` 展开 ACK；
- 拒绝确认尚未发送数据的非法 ACK；
- 更新对端通告窗口；
- 从 outstanding queue 移除已被累计确认的 segment；
- 更新 `bytes_in_flight`；
- 收到推进 ACK 后重置 RTO 和连续重传计数；
- 释放窗口后继续调用 `fill_window()`。

#### 重传计时器

`tick()`：

- 只关注最早的未确认 segment；
- 超过 RTO 后重新发送该 segment；
- 连续超时采用指数退避，将 RTO 翻倍；
- 记录连续重传次数，供连接层判断是否放弃连接；
- 没有 outstanding segment 时停止计时。

### 对应知识点

- TCP 发送滑动窗口；
- 流量控制与对端 advertised window；
- 累计 ACK 和在途字节统计；
- SYN、payload、FIN 在 sequence space 中的排列；
- 超时重传、RTO 和指数退避；
- zero-window probe；
- 队列管理和计时器驱动的状态变化；
- 发送窗口与拥塞窗口的区别：本 Lab 实现的是接收端流量控制和可靠重传，不是完整 TCP 拥塞控制。

### 验证命令

```bash
cmake --build cmake-build-debug-wsl --target check_lab3
```

## Lab 4：TCPConnection

### 修改的主要代码

- `libsponge/tcp_connection.cc`
- `libsponge/tcp_connection.hh`
- 配套调整 `tcp_sender.*`、`tcp_receiver.cc`
- 少量调整 `apps/webget.cc`、`libsponge/util/tun.cc` 和测试配置

本 Lab 围绕连接层新增约 224 行核心代码，并通过多个提交修复主动/被动关闭、RST、重传窗口和 loopback 场景。

### 实现内容

`TCPConnection` 把 `TCPSender` 与 `TCPReceiver` 组合成双向 TCP 连接：

- `connect()`：主动打开并触发 SYN；
- `segment_received()`：同时处理对端 ACK 和接收方向 payload/SYN/FIN；
- `push_segments_out()`：将 receiver 的 ACK number 和 window size 附到 sender 产生的 segment；
- 对需要立即响应但没有数据可发的情况产生空 ACK segment；
- 支持被动打开和同时打开相关状态；
- 处理非法 ACK、RST 和 SYN_SENT 等边界条件；
- `write()`、`end_input_stream()`：把应用写入和半关闭传给 sender；
- `tick()`：推进 sender 定时器和连接空闲时间；
- 连续重传超过上限时发送 RST 并非正常关闭；
- 双向流完成后根据 linger 规则关闭连接；
- 被动关闭时可以取消 linger；主动关闭通常等待 `10 × RTO`；
- 对象析构时若连接仍活跃，执行带 RST 的非正常关闭。

### 对应知识点

- TCP 有限状态机和连接生命周期；
- 三次握手的主动打开与被动打开；
- 全双工连接由两个单向字节流组成；
- ACK 可以搭载在数据 segment 上；
- FIN 半关闭、双方分别结束发送方向；
- 正常关闭、RST 异常关闭和超时放弃；
- linger/TIME-WAIT 思想及 `10 × RTO` 等待；
- 事件驱动设计：输入 segment、应用写入和时间 tick 都会推动状态机；
- 将 receiver 与 sender 两个独立组件协调为一个协议实体。

### 验证命令

```bash
cmake --build cmake-build-debug-wsl --target check_lab4
```

该 target 会准备 `tun144` 和 `tun145`，首次运行可能请求 sudo 密码。

## Lab 5：NetworkInterface 与 ARP

### 修改的主要代码

- `libsponge/network_interface.cc`
- `libsponge/network_interface.hh`
- 少量调整 `apps/webget.cc` 和 `libsponge/util/tun.cc`

核心网络接口逻辑约新增 113 行。

### 实现内容

#### 发送 IPv4 数据报

`send_datagram(dgram, next_hop)`：

- 把 IPv4 数据报序列化为 Ethernet frame payload；
- 根据 next-hop IPv4 地址查询 ARP 缓存；
- 命中有效缓存时填写目的 MAC 并立即发出；
- 未命中时发送广播 ARP request；
- 将等待解析 MAC 地址的 IPv4 frame 暂存在队列中。

#### 接收 Ethernet 帧

`recv_frame()`：

- 只接受目的 MAC 为本机或广播地址的帧；
- 对 IPv4 frame 解析并向上层返回 `InternetDatagram`；
- 从合法 ARP message 学习发送端的 IP→MAC 映射；
- 收到针对本机 IP 的 ARP request 时发送单播 ARP reply；
- 获得映射后发送队列中等待该地址的 IPv4 frame。

#### ARP 定时逻辑

- 学到的 ARP cache entry 有效期为 30 秒；
- 对未解析地址的 ARP request 至多每 5 秒重试一次；
- `tick()` 使用外部传入的毫秒数推进逻辑时钟。

### 对应知识点

- 分层网络模型：IPv4 数据报封装进 Ethernet 帧；
- MAC 地址只用于本地链路，IP 地址用于跨网络寻址；
- 最终目的 IP 与 next-hop IP 的区别；
- ARP request 广播和 ARP reply 单播；
- ARP cache、老化时间和请求限速；
- 未完成地址解析时的数据报排队；
- 二层 EtherType、帧过滤、序列化和解析；
- TUN 与 TAP 的区别：TUN 面向三层 IP，TAP 面向二层 Ethernet。

### 验证命令

```bash
cmake --build cmake-build-debug-wsl --target check_lab5
```

该 target 会准备 `tap10`，首次运行可能请求 sudo 密码。

## Lab 6：Router

### 修改的主要代码

- `libsponge/router.cc`
- `libsponge/router.hh`

这是代码量最小但路由概念最集中的一项：核心实现约新增 38 行。

### 实现内容

#### 路由表

`add_route()` 保存每条路由的：

- IPv4 prefix；
- prefix length；
- 可选 next hop；
- 输出 interface 编号。

#### 转发单个数据报

`route_one_datagram()`：

- 遍历路由表，找出与目的地址匹配且 prefix 最长的条目；
- 没有匹配路由时静默丢弃；
- TTL 小于等于 1 时丢弃，避免继续转发；
- 否则将 TTL 减 1；
- 有 next hop 时发给网关；
- 直连路由没有 next hop，此时把最终目的 IP 作为下一跳；
- 通过选中的 `NetworkInterface::send_datagram()` 发出。

`route()` 则轮询所有异步网络接口，把每个入口队列中的数据报逐一转发。

### 对应知识点

- IPv4 forwarding 与路由表；
- CIDR prefix 和 subnet mask；
- 最长前缀匹配（Longest Prefix Match）；
- `/0` 默认路由的特殊边界；
- next hop、输出接口和直连网络；
- TTL 的作用和逐跳递减；
- 无路由或 TTL 到期时的数据报丢弃；
- 控制面与数据面的简单区分：`add_route()` 配置路由，`route()` 执行转发；
- 多接口队列和协议层组合。

### 验证命令

```bash
cmake --build cmake-build-debug-wsl --target check_lab6
```

## 模块依赖关系

| 上层模块 | 直接依赖 | 依赖原因 |
|---|---|---|
| `StreamReassembler` | `ByteStream` | 将连续数据输出成可靠字节流 |
| `TCPReceiver` | `WrappingInt32`、`StreamReassembler` | 展开 TCP 序号并重组 payload |
| `TCPSender` | `WrappingInt32`、`ByteStream` | 从应用流取数据并生成带序号 segment |
| `TCPConnection` | `TCPSender`、`TCPReceiver` | 协调两个方向并管理完整 TCP 状态 |
| `NetworkInterface` | Ethernet、ARP、IPv4 | 在三层数据报和二层帧之间转换 |
| `Router` | `NetworkInterface` | 根据路由表选择下一跳和输出接口 |

这也是各 Lab 必须按顺序完成的原因：后一个 Lab 会直接复用前一个 Lab 建立的状态语义和接口契约。

## 跨 Lab 最值得掌握的不变量

1. `ByteStream` 中已缓存字节数不能超过 capacity。
2. 重组器的输出缓冲和未组装数据共同受 capacity 限制。
3. TCP 的 stream index 不包含 SYN/FIN，sequence number 空间包含它们。
4. receiver 的 ACK 始终表示下一个期待接收的 sequence number。
5. sender 的 `bytes_in_flight` 只统计已发送但尚未累计确认的 sequence-space 长度。
6. sender 不能超出对端通告窗口；零窗口探测是特例。
7. IPv4 目的地址在逐跳转发中保持不变，但二层目的 MAC 每一跳都可能变化。
8. router 选择最长匹配前缀，并在转发前检查和递减 TTL。

## 查看某个 Lab 的实际改动

```bash
# 例：查看 Lab 3 从 starter 合入到通过测试之间的提交
git log --oneline --reverse 0829cac..95b57c1

# 查看文件统计
git diff --stat 0829cac..95b57c1

# 查看完整代码差异
git diff 0829cac..95b57c1 -- libsponge/tcp_sender.cc libsponge/tcp_sender.hh
```

其他 Lab 的提交范围可使用本文开头表格中的值替换。
