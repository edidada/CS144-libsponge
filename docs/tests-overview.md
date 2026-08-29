# CS144 测试用例说明：作用、代码与知识点笔记

## 1. 测试体系总览

本仓库的测试分 **三层**，层层递进，对应不同 Lab 的验收：

| 层次 | 运行方式 | 是否需要 root | 验证内容 |
| --- | --- | --- | --- |
| 单元/状态机测试（CTest，进程内） | `cmake --build ... --target check_labX` | 否 | Lab 0-5 各组件（ByteStream/Reassembler/Receiver/Sender/TCPConnection/NetworkInterface）+ pcap 解析 |
| 端到端测试（txrx.sh） | `sudo` 跑 `t_*` | 是（tun144/tun145） | Lab 4 TCP 栈真实跑通（UDP/IPv4 两种承载） |
| 应用级测试 | `apps/` 下可执行文件 | webget 需要 tap10 | `webget`（真实 HTTP）、`tcp_benchmark`（吞吐）、`network_simulator`（Lab6 路由） |

测试统一由 `etc/tests.cmake` 注册到 CTest，入口目标是 `check` / `check_lab0` ~ `check_lab6`（见第 6 节）。

## 2. 目录与构建注册

```text
tests/                # 46 个测试可执行文件 + 测试 harness
├── CMakeLists.txt    # add_test_exec 注册表 + spongechecks 静态库
├── *test_harness.hh  # 各 Lab 的测试框架（第 3 节）
├── tcp_expectation.hh / send_equivalence_checker.hh   # Lab4 的断言与等价性检查
├── fsm_retx.hh       # 重传测试共享工具
├── webget_t.sh       # webget 测试脚本（参数回显 + -x 真实模式）
└── ipv4_parser.data  # pcap 数据文件（tcp_parser/ipv4_parser 用）

doctests/             # Lab0 热身：address_dt / parser_dt / socket_dt
etc/tests.cmake       # CTest 注册 + check_lab0-6 聚合目标
apps/                 # tcp_ipv4 / tcp_udp / webget / tun / network_simulator ...
txrx.sh               # Lab4 端到端对跑脚本
```

`tests/CMakeLists.txt` 把通用 harness 编译成静态库 `spongechecks`，测试可执行文件统一用 `add_test_exec` 宏生成（链接 `spongechecks` + `sponge` 两个库）：

```1:7:tests/CMakeLists.txt
add_library (spongechecks STATIC send_equivalence_checker.cc tcp_fsm_test_harness.cc byte_stream_test_harness.cc network_interface_test_harness.cc)

macro (add_test_exec exec_name)
    add_executable ("${exec_name}" "${exec_name}.cc")
    target_link_libraries ("${exec_name}" spongechecks ${ARGN})
    target_link_libraries ("${exec_name}" sponge ${ARGN})
endmacro (add_test_exec)
```

## 3. Harness 设计模式（核心知识点）

每个 Lab 一个测试框架，模式统一：**"步骤"（Step）对象 + 依次 execute + 失败打印步骤历史**。

- 步骤分两类：**Action**（对被测对象施加输入，如 `Write`、`SubmitSegment`、`Tick`、`SendSegment`）和 **Expectation**（断言状态，如 `ExpectNoSegment`、`ExpectState`、`BytesWritten`）。
- `harness.execute(step)` 执行步骤；Expectation 不满足就抛异常，main 里 catch 后把**已执行步骤列表**逐行打印出来，配合 `cerr` 的 `test failed:` 提示定位是哪一步出的问题。
- 本质是**用代码把状态机的每一条路径走一遍**：每步动作 + 断言组成一条合法/非法路径。

各 Lab 的 harness 及代表作：

| Lab | Harness | 典型步骤 |
| --- | --- | --- |
| 0 | `byte_stream_test_harness.hh` | `Write`/`Pop`/`EndInput` + `BufferSize`/`BytesWritten`/`RemainingCapacity`/`Eof` |
| 1 | `fsm_stream_reassembler_harness.hh` | `SubmitSegment{data,index}` + `BytesAssembled`/`BytesAvailable`/`AtEof` |
| 2 | `receiver_harness.hh` | `SegmentArrives` + `ExpectAckno`/`ExpectWindow`/`ExpectBytes`/`ExpectState` |
| 3 | `sender_harness.hh` | `WriteBytes`/`Tick`/`AckReceived` + `ExpectSegment`/`ExpectBytesInFlight`/`ExpectState` |
| 4 | `tcp_fsm_test_harness.hh` + `tcp_expectation.hh` | `TCPAction`/`TCPExpectation` 两类步骤 + `TestFD` 模拟网卡 |
| 5 | `network_interface_test_harness.hh` | `SendDatagram`/`ReceiveFrame`/`ExpectFrame`/`Tick`（ARP 缓存 TTL） |

**Lab4 的两个关键机制**：

1. `TestFD` 用 `socketpair(AF_UNIX, SOCK_SEQPACKET, 0)` 模拟"网卡"——SOCK_SEQPACKET 保留消息边界，一段一个段，天然不会把两个 TCP 段粘在一起，比字节流 socket 更适合测段级收发。
2. `tcp_expectation.hh` 里 `TCPExpectation` / `TCPAction` 都继承 `TCPTestStep`，`ExpectSegment` 可链式设置 `with_syn/with_ack/with_payload_size/with_data/with_seqno`：

```15:27:tests/tcp_expectation.hh
struct TCPExpectation : public TCPTestStep {
    virtual ~TCPExpectation() {}
    std::string to_string() const { return "Expectation: " + description(); }
    virtual std::string description() const { return "description missing"; }
    virtual void execute(TCPTestHarness &) const {}
};

struct TCPAction : public TCPTestStep {
    virtual ~TCPAction() {}
    std::string to_string() const { return "Action:      " + description(); }
    virtual std::string description() const { return "description missing"; }
    virtual void execute(TCPTestHarness &) const {}
};
```

**严格版 vs relaxed 版**：`fsm_connect`/`fsm_listen`/`fsm_retx`/`fsm_ack_rst` 等有严格版和 `_relaxed` 版。严格版用 `SendEquivalenceChecker`（`send_equivalence_checker.hh`：内部 `as`/`bs` 两个 deque 分别收集**本实现**与**参考实现**发出的段，`check_empty` 要求两者逐段完全一致）——对段发送顺序、合并不都做强制要求，因此测试更苛刻但更容易受实现细节影响；`etc/tests.cmake` 里默认注册的都是 relaxed 版（严格版 `ec_*` 被注释掉）。`fsm_retx.hh` 的 `check_segment` 则是重传场景的共享断言工具（校验段内容 + 可选"仅一个段"）。

## 4. 各 Lab 测试用例明细

### Lab 0 —— `byte_stream_*` + `webget`
- `byte_stream_construction`：构造 + 初始状态断言（空、未结束、容量 15）。代码模式见 `tests/byte_stream_construction.cc`：每个测试块 `{ ... }` 一个场景，`test.execute(...)` 逐步断言。
- `byte_stream_one_write` / `two_writes` / `capacity` / `many_writes`：写读、容量边界（超额写入丢弃/拒绝）、随机压力（`get_random_generator()` + `uniform_int_distribution`）。
- `t_webget`：真实 HTTP 客户端（`tests/webget_t.sh`），走 `tap10` → 主机 NAT → 互联网（超时 30s），验证 Lab 0 的 `WebGetRequest` 实现。

### Lab 1 —— `fsm_stream_reassembler_*`
8 个用例覆盖重排器全部行为：`cap`（容量约束）、`single`/`seq`（基础段）、`dup`（重复段）、`holes`（乱序空洞，**先提交 `b`@1 再提交 `a`@0**，中间是空洞，见 `tests/fsm_stream_reassembler_holes.cc`）、`many`（大量段）、`overlapping`（重叠段合并）、`win`（窗口）。

```14:21:tests/fsm_stream_reassembler_holes.cc
ReassemblerTestHarness test{65000};

test.execute(SubmitSegment{"b", 1});

test.execute(BytesAssembled(0));
test.execute(BytesAvailable(""));
test.execute(NotAtEof{});
```
知识点：乱序段先缓存、`BytesAssembled` 只统计**已连续**字节数；EOF 必须等空洞补齐后才生效（`holes` 第 34-47 行验证 `with_eof(true)` 的段先到但不触发 EOF）。

### Lab 2 —— `recv_*` + `wrapping_*`
- `wrapping_integers_wrap` / `unwrap` / `cmp`：32 位序列号与 64 位绝对序号互转、环绕比较——`WrappingInt32` 的取模语义（`wrap` 时 `isn` 起点偏移，`unwrap` 用 `checkpoint` 消除 2^32 歧义）。
- `recv_connect`：SYN 握手（首个段必须 SYN + ackno = isn+1）。
- `recv_transmit` / `recv_window` / `recv_reorder`：数据收包、窗口大小通告、乱序重组。
- `recv_close`：FIN 之后不可再写，EOF 语义。

### Lab 3 —— `send_*`
- `send_connect`：发 SYN（seqno = ISN），状态机 `SYN_SENT`。
- `send_transmit`：填满窗口发送、段大小上限 1452（`TCPConfig::MAX_PAYLOAD_SIZE`）。
- `send_retx`：**超时重传 + 指数退避**——`Tick{t}` 驱动时间，超时后重发 SYN，重试间隔翻倍，超过 `MAX_RETX_ATTEMPTS` 置 `max_retx_exceeded`。见 `tests/send_retx.cc` 前两段（SYN 重传两次后收到 ACK、以及直到放弃）。
- `send_ack` / `send_window` / `send_close`：ACK 推进窗口、0 窗口探测、FIN 发送与重传。

### Lab 4 —— `fsm_*` + txrx.sh 端到端
单元层（免 root，进程内）：
- `fsm_connect`/`fsm_listen`：主动/被动连接（含 relaxed 变体）。
- `fsm_active_close`/`fsm_passive_close`：四次挥手（先关 vs 对端先关）。
- `fsm_retx`/`fsm_retx_win`：数据重传（`fsm_retx.hh` 共享 `check_segment`）。
- `fsm_ack_rst`/`fsm_ack_rst_win`：ACK/RST 处理（乱序 ACK 忽略、RST 半开连接）。
- `fsm_winsize`、`fsm_loopback`/`fsm_loopback_win`、`fsm_reorder`：窗口、数据回环（随机大流量往返，见 `tests/fsm_loopback.cc`：循环 `Write` → `ExpectSegmentAvailable` → 逐段 `SendSegment` 回灌）、乱序。

端到端层（`txrx.sh`，需要 tun）：见第 5 节。

### Lab 5 —— `net_interface`
`net_interface.cc`：模拟一个 NetworkInterface 收 `ARP request/reply`、IP 广播、以及 ARP 缓存 TTL 过期（`Tick` 推进）。核心知识点：广播地址、ARP 学习/回复、缓存 30s TTL 刷新。

### Lab 6 —— `network_simulator`（router_test）
`apps/network_simulator.cc`：模拟 6 个网段的路由器，验证最长前缀匹配路由表 + TTL 递减 + ARP 邻居学习。

### 解析类 —— `tcp_parser` / `ipv4_parser`
读 `tests/ipv4_parser.data`（pcap）逐包解析校验；`tcp_parser.cc` 另用**随机生成的合法头**（32 次迭代，随机字节 + 重算校验和）验证 `TCPHeader::parse` 的字段解析与错误检测。

### 应用级 —— `tcpsocket_test` / `tcp_benchmark` / `tun`
- `tcpsocket_test`：127.0.0.1 真实 socket 回环。
- `tcp_benchmark`：纯内存把两个 `TCPConnection` 互喂，测吞吐（本机约 2.46 Gbit/s，乱序 0.96）。
- `tun`：抓 tun144 上的裸 IP 包 hexdump + 解析打印，用于端到端调试。

## 5. txrx.sh 端到端测试（Lab4 验收核心）

`txrx.sh` 拉起两个进程对跑：测试方用 `tcp_ipv4`（自研 TCP 栈 over tun）或 `tcp_udp`（UDP socket），参考方用官方实现，校验数据一致 + 关闭时序。测试名即参数编码：

```text
t_icS_1M_32k
 │││ │  └─ w 窗口大小（32K/8K/1）
 │││ └──── d 数据量（1M/128K/32K/16）
 ││└────── S=发送  R=接收  D=双向（方向：由 c/s 方发起）
 │└─────── c=client 发起  s=server 发起
 └──────── u=UDP（tcp_udp）  i=IPv4（tcp_ipv4，走 tun144/tun145）
```

可选后缀：`n` 禁用 Nagle（`-icnS`），`l`/`L` 分别给两条方向加 10% 丢包（`LOSS_RATE=0.1`），`-lL` 双向丢包。注册的用例矩阵覆盖：数据量 16B~1MB × 窗口 1B~32KB × 丢包 0/10% × 双向，共 100+ 个 `t_*` 测试。

```76:79:etc/tests.cmake
add_test(NAME t_ucS_1M_32k           COMMAND "${PROJECT_SOURCE_DIR}/txrx.sh" -ucSd 1M -w 32K)
add_test(NAME t_ucS_128K_8K          COMMAND "${PROJECT_SOURCE_DIR}/txrx.sh" -ucSd 128K -w 8K)
add_test(NAME t_ucS_16_1             COMMAND "${PROJECT_SOURCE_DIR}/txrx.sh" -ucSd 16 -w 1)
add_test(NAME t_ucS_32K_d            COMMAND "${PROJECT_SOURCE_DIR}/txrx.sh" -ucSd 32K)
```

## 6. check_labX 聚合目标

`etc/tests.cmake` 末尾用 CTest 正则聚合出验收目标（`-R` 匹配测试名）：

| 目标 | 前置（需要 root） | 覆盖 |
| --- | --- | --- |
| `check_lab0` | - | `t_webget` + `t_byte_stream` + `_dt` |
| `check_lab1` | - | + `t_strm_reassem_` |
| `check_lab2` | - | + `t_recv_` + `t_wrapping_` |
| `check_lab3` | - | + `t_send_` |
| `check_lab4` | `tun.sh check 144 145` | 全部 `^t_`（含 txrx.sh 全部端到端） |
| `check_lab5` | `tap.sh check 10` | `^t_webget` + `^arp_` |
| `check_lab6` | - | `^arp_` + `^router_` |
| `check` | 两个脚本 | `^t_` + `^arp_` + `^router_` |

要点：lab4/5 的 `check` 目标自动执行 `tun.sh`/`tap.sh check`（幂等补建设备），所以必须 `sudo`；lab0-3、6 全程免 root。`timeout 10/30` 给每个测试限时。

## 7. 知识点笔记汇总

1. **状态机测试 = 动作 + 断言序列**：每个 `{...}` 块是一条状态路径，harness 记录步骤历史便于定位失败点。
2. **SOCK_SEQPACKET 模拟网卡**：保留段边界，天然符合"一段一读"，避免 TCP 字节流粘包干扰段级断言。
3. **等价性检查器（SendEquivalenceChecker）**：严格测试把自研实现与参考实现**逐段对比**，relaxed 版只做关键断言——两者互补，官方注册 relaxed 保证可过性。
4. **WrappingInt32 环绕语义**：`unwrap` 用 checkpoint 就近取值，覆盖 2^32 回绕边界（`wrapping_*` 测试专门构造跨环绕场景）。
5. **重传与指数退避**：RTO 初始随机（10~10000ms），超时翻倍；`Tick` 是测试里"拨快时钟"的手段。
6. **真实网络验证靠 tun/tap**：单元测试喂的是人工构造的段；`txrx.sh` 让自研栈发出的包走真实内核 IP 栈（tun144↔tun145），webget 走 tap10 + NAT 访问公网。
7. **随机性**：测试大量使用 `get_random_generator()`（统一种子流），保证压力场景可复现又覆盖多样输入。
8. **pcap 解析测试**：`tcp_parser`/`ipv4_parser` 既验真实抓包数据，又用随机合法头验解析器健壮性（含校验和）。

## 8. 常用命令

```bash
# 免 root 的单 Lab 验收
cmake --build cmake-build-debug-wsl --target check_lab1
cmake --build cmake-build-debug-wsl --target check_lab3

# 需要 root 的端到端（自动创建 tun/tap）
sudo cmake --build cmake-build-debug-wsl --target check_lab4

# 单个测试
ctest --test-dir cmake-build-debug-wsl -R t_loopback --output-on-failure

# 手动端到端对跑（Lab4）
sudo cmake-build-debug-wsl/apps/tcp_ipv4 -l -d tun145 4000
sudo cmake-build-debug-wsl/apps/tcp_ipv4 -d tun144 169.254.145.9 4000
```

## 9. 每个可单独执行的测试逐一说明

全部测试由 `tests/CMakeLists.txt` 的 `add_test_exec` 注册（46 个）+ `doctests/CMakeLists.txt`（3 个）+ `tests/webget_t.sh`（1 个脚本）。CTest 里统一加 `t_` 前缀（如 `byte_stream_construction` 在 ctest 里叫 `t_byte_stream_construction`），可用 `ctest -R t_xxx` 单独跑。

### 解析类（不依赖 Lab 组件，最先写）

- **`tcp_parser`** —— 从 `tests/ipv4_parser.data`（pcap）逐包解析 TCP 头字段；再用**随机字节拼出的合法头**（32 轮，重算校验和）验证 `TCPHeader::parse` 的字段还原与错误检测。涉及：`tests/tcp_parser.cc`、`tests/tcp_parser.hh`（数据文件）、`libsponge/tcp_header.{cc,hh}`、`InternetChecksum`。知识点：校验和（16 位反码和）、头部解析健壮性。
- **`ipv4_parser`** —— 同上，对象是 IPv4 头（随机头固定 `0x45` 版本/长度 + 重算头校验和）。涉及：`tests/ipv4_parser.cc`、`libsponge/ipv4_header.{cc,hh}`。知识点：IHL/总长度/校验和字段、pcap 数据驱动的解析验证。

### Lab 0 —— ByteStream（`tests/byte_stream_*.cc` + `webget_t.sh`）

- **`byte_stream_construction`** —— 构造 `ByteStream` 后立即断言初始状态：空缓冲、未到 EOF、`BytesRead=0`。涉及：`libsponge/byte_stream.{cc,hh}`。知识点：容量初始化、`eof`/`input_ended` 分离。
- **`byte_stream_one_write`** —— 一次 `Write{"cat"}` 后逐项断言（`Peek`/`BytesWritten=3`/`RemainingCapacity=12`），再 `EndInput` → `Pop` 直到 EOF。知识点：写读计数、EOF 后不能写。
- **`byte_stream_two_writes`** —— 两次写（`cat`+`tac`）、两次读，验证连续写读与计数累计。知识点：缓冲不丢序。
- **`byte_stream_capacity`** —— 容量 2 时 `Write{"cat"}` 只成功 2 字节（`with_bytes_written(2)`），写满后再写 0 字节被拒，`Peek` 只返回 `ca`。知识点：容量边界（超量写入被丢弃/截断）。
- **`byte_stream_many_writes`** —— 1000 轮随机长度写 + 交错读，容量 = 最大写 × 次数。知识点：压力测试 + 统一种子随机流。
- **`webget_t.sh`**（CTest 名 `t_webget`）—— 真实 HTTP 客户端：`webget <host> /` 打印响应头。普通模式只是参数回显校验；`-x` 模式走 `tap10` → 主机 NAT 访问公网（需 root，超时 30s）。知识点：Lab0 的 `WebGetRequest` 用真实网络验收。

### Lab 1 —— StreamReassembler（`tests/fsm_stream_reassembler_*.cc`）

- **`fsm_stream_reassembler_cap`** —— 容量 2：每段"消费即释放"容量（`BytesAssembled` 推进后能继续收新段）；满容量时再来的段被丢弃。知识点：容量按**未组装字节**计。
- **`fsm_stream_reassembler_single`** —— 单段提交：无段时 `BytesAssembled=0`；提交 `a`@0；提交 `a`@0+EOF 后 `AtEof`。知识点：EOF 标记随段携带。
- **`fsm_stream_reassembler_seq`** —— 多段按序提交（`abcd`@0、`efgh`@4…），逐步组装并 `NotAtEof`。知识点：连续索引推进。
- **`fsm_stream_reassembler_dup`** —— 重复段：已组装的 `abcd`@0 重发后 `BytesAssembled` 不变、`BytesAvailable("")`；与已读部分重叠的段只产生新字节。知识点：去重 + 已读区间剪裁。
- **`fsm_stream_reassembler_holes`** —— 乱序空洞：先 `b`@1 后 `a`@0（`BytesAssembled=0` 缓存），补齐后 `ab` 组装；带 EOF 的段先到但空洞未补时不触发 EOF。知识点：乱序缓存 + EOF 等待补齐。
- **`fsm_stream_reassembler_many`** —— 128 段随机乱序、随机长度压力（32 轮，不经过 harness，直接驱动 `StreamReassembler`）。知识点：大数据量乱序重组。
- **`fsm_stream_reassembler_overlapping`** —— 重叠合并三种情形：与**已组装未读**区重叠、与**已读**区重叠（只补新字节）、与**缓存区**重叠（合并后组装）。知识点：重叠区间归并。
- **`fsm_stream_reassembler_win`** —— 128 段窗口压力：每段在窗口内随机偏移 + 长度，验证只保留窗口内字节（越界丢弃）。知识点：接收窗口裁剪。

### Lab 2 —— WrappingInt32 + TCPReceiver（`tests/wrapping_integers_*.cc`、`tests/recv_*.cc`）

- **`wrapping_integers_wrap`** —— `wrap`：绝对序号 → 32 位（含 `isn` 偏移、取模）。知识点：`WrappingInt32` 的加法定义。
- **`wrapping_integers_unwrap`** —— `unwrap`：32 位 → 绝对序号，用 `checkpoint` 消除 2^32 歧义，覆盖跨环绕（如 checkpoint 在 2^32 附近的取近值）。知识点：checkpoint 就近原则。
- **`wrapping_integers_cmp`** —— `operator<`/`>`：用 `int32_t` 差值判断环绕序（`static_cast<int32_t>(b-a)>0` 表示 a 在后）。知识点：环绕比较的差值技巧。
- **`recv_connect`** —— `segment_received(SYN)`：首段必须 SYN、`ackno = isn+1`、状态 `SYN_RECV`。涉及：`libsponge/tcp_receiver.{cc,hh}`。知识点：握手期 ackno 语义。
- **`recv_transmit`** —— 收数据段：`expect_bytes` 组装、`ackno` 推进到已连续位置。知识点：接收端累积 ACK。
- **`recv_window`** —— 窗口大小通告：`expect_window(0/1/…)`，**已组装未读**的字节不占窗口，读走才释放。知识点：窗口 = 容量 − 未读字节数。
- **`recv_reorder`** —— 乱序收段：先 2 后 1，`stream_out` 只出连续部分。知识点：依赖 Lab1 的 Reassembler。
- **`recv_close`** —— FIN 处理：收到 FIN 后 `input_ended`、EOF；FIN 之后再来的数据无效。知识点：FIN 占用一个序号。

### Lab 3 —— TCPSender（`tests/send_*.cc`）

- **`send_connect`** —— `connect()` 后发 SYN（`seqno = ISN`、无 payload），状态 `SYN_SENT`；`fill_window` 不重复发。知识点：ISN 初始化。
- **`send_transmit`** —— 填窗发送：窗口够大时一次发满，段 payload ≤ 1452（`TCPConfig::MAX_PAYLOAD_SIZE`）；窗口为 0 时也发探测段。知识点：分段上限 + 0 窗口探测。
- **`send_retx`** —— 超时重传：`Tick` 拨动时钟，超时重发未确认段、RTO 指数翻倍、超过 `MAX_RETX_ATTEMPTS` 后 `max_retx_exceeded` 置位。知识点：重传定时器与退避。
- **`send_ack`** —— ACK 处理：合法 ACK 推进 `bytes_in_flight` 并清 RTO；重复/非法 ACK 不推进。知识点：累计确认、`_ack_received` 边界。
- **`send_window`** —— 窗口边界：0 窗口时全部挂起、小窗口部分发送、`fill_window` 遵守 `window_size`。知识点：窗口是发送上限。
- **`send_close`** —— `close()` 后发 FIN（占序号），FIN 未确认则重传；关闭后不再接受新数据。知识点：FIN 与发送方向结束。

### Lab 4 —— TCPConnection 状态机（`tests/fsm_*.cc`）

- **`fsm_connect`**（严格）/ **`fsm_connect_relaxed`** —— 主动连接：`Connect` → 发 SYN → 纯 ACK（应**忽略**，见 RFC 68 页）→ 收到 SYN/ACK → `SYN_RCVD` → ACK → `ESTABLISHED`。严格版还要与参考实现逐段对比（`SendEquivalenceChecker`）。知识点：三次握手全路径 + LISTEN 外收到 ACK 的处理分歧（严格 vs relaxed）。
- **`fsm_listen`** / **`fsm_listen_relaxed`** —— 被动连接：`Listen` 后非 SYN 数据丢弃（不回包）→ 收到 SYN 回 SYN/ACK（`ackno=1`）→ 收到 ACK 进入 `ESTABLISHED`；旧序号 ACK 会得到重发。知识点：被动握手 + 旧包重传 ACK。
- **`fsm_active_close`** —— 主动关闭：`in_time_wait` 状态下 `Tick(10×rt_timeout)` 后离开 `TIME_WAIT` 到 `CLOSED`；`CLOSING` 收 ACK → `TIME_WAIT` → 超时关闭。知识点：TIME_WAIT 时长 = 2×MSL（测试按 10×rt_timeout）。
- **`fsm_passive_close`** —— 被动关闭：`LAST_ACK` 收 ACK → `CLOSED`；`CLOSE_WAIT` → `Close()` → 发 FIN 进 `LAST_ACK`，FIN 丢失后**超时重传**（`rt_timeout` 边界：提前 2ms 不重发），收 ACK → `CLOSED`。知识点：挥手全路径 + FIN 重传。
- **`fsm_ack_rst`**（严格）—— ACK/RST 语义：`LISTEN` 收到 ACK 应回 RST（进 `RESET`）；`SYN_SENT` 收到不可接受 ACK 回 RST。**`fsm_ack_rst_relaxed`**：同一场景改为忽略（`LISTEN` 收 ACK 不响应）。`fsm_ack_rst_win` / `_relaxed`：带窗口检查的变体。知识点：RFC 对 LISTEN 态 ACK 的分歧——官方验收用 relaxed。
- **`fsm_retx`** / **`fsm_retx_relaxed`** —— 数据重传：established 写 `asdf`，`rt_timeout` 边界内不重发、超时后按 `rt_timeout<<i` 指数重发直至放弃；注释特意要求兼容"write 立即发段"与"tick 才发段"两种实现。`fsm_retx_win`：带窗口的变体。知识点：重传定时器 + 退避 + 放弃上限。
- **`fsm_reorder`** —— 乱序回灌：established 下把大数据切段 `shuffle` 后乱序喂回，验证端到端重组（32 轮随机）。知识点：接收窗口 + Reassembler 协同。
- **`fsm_loopback`** / **`fsm_loopback_win`** —— 数据回环：把发出的段（含 ACK）原样回灌给同一连接，验证全双工数据往返；`_win` 版乱序回灌。知识点：发送/接收在一条连接里互不干扰。
- **`fsm_winsize`** —— 窗口通告与发送上限：listen → established 后断言 SYN/ACK 里 `win=recv_capacity`，ACK 前最多发窗口字节（随机窗口 2048~34816，32 轮）。知识点：`window_size` 通告与填充上限一致。

### Lab 5 —— NetworkInterface（`tests/net_interface.cc`）

- **`net_interface`** —— 模拟网卡：`SendDatagram` → 需先 ARP 解析才发 IP 帧；收到 ARP request 回 reply 并缓存学习；广播地址；`Tick` 驱动 ARP 缓存 30s TTL 过期后重新解析。知识点：ARP 请求/应答/缓存/过期，Ethernet 帧组装（`NetworkInterface` 也是 `t_webget` 能上网的前提）。

### Lab 6 —— 路由器（`apps/network_simulator`）

- **`network_simulator`**（CTest 名 `t_router_test`）—— 模拟 6 网段路由器：路由表最长前缀匹配、TTL 递减与超时丢弃、ARP 邻居学习转发。知识点：转发决策 + 路由表优先级。

### doctests（Lab0 热身，`doctests/` 目录）

- **`address_dt`** —— `Address` 类：IPv4/IPv6 字符串 ↔ 二进制地址互转、主机名解析。涉及：`libsponge/util/address.{cc,hh}`。
- **`parser_dt`** —— 解析工具：`BufferReader` 读字节序、`InternetChecksum` 校验和、`ParseResult` 错误处理。涉及：`libsponge/util/buffer.{cc,hh}` 等。
- **`socket_dt`** —— `TCPSocket`：本地回环连接、读写与关闭语义。涉及：`libsponge/util/socket.{cc,hh}`。

### 应用级（`apps/`，既是 app 也是验收工具）

- **`tcpsocket_test`** —— 127.0.0.1 真实 socket 回环，验证系统 socket 栈与 `Socket` 封装。
- **`tcp_benchmark`** —— 纯内存把两个 `TCPConnection` 互喂测吞吐（本机约 2.46 Gbit/s），验证性能。
- **`tun`** —— 打开 `tun144` 抓裸 IP 包 hexdump + 解析打印，端到端调试工具。
- **`tcp_ipv4`** —— Lab4 端到端主角：自研 TCP 栈 over tun（`-l` 监听 / `-d` 设备 / `-a` 地址）。
- **`tcp_udp`** —— 基于 UDP socket 的对照实现，txrx.sh `-u` 模式用它验证 UDP 承载下数据通路。
- **`webget`** —— Lab0 HTTP 客户端，`t_webget` 的可执行载体。
