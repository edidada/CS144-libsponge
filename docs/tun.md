# tun（TUN 虚拟网络设备）在 CS144 Lab 中的作用

## 1. tun 是什么

tun 是 Linux 内核提供的**三层（网络层）虚拟网络设备**：它没有物理网线，一端连着内核协议栈，另一端由用户态程序打开 `/dev/net/tun` 后通过 `read()` / `write()` 读写。用户态程序往设备写入的数据，会被内核当作"从该网卡收到的 IP 包"进入内核协议栈；内核路由到该网卡的 IP 包，也会被"丢给"用户态程序读走。

与 tun 对应的是 **tap**：tap 是**二层（链路层）虚拟设备**，读写的是完整的 Ethernet 帧（含 MAC 头），所以需要 ARP、Ethernet 头部解析。tun 读写的是裸 IPv4 包，跳过二层。

```text
用户态程序 ──read/write── /dev/net/tun ──ioctl(TUNSETIFF)── tun144
                                                              │
                                                    内核协议栈（路由/转发）
```

本仓库用 `libsponge/util/tun.cc` 封装了这一步：`TunTapFD` 构造时 `open("/dev/net/tun", O_RDWR)` 并 `ioctl(TUNSETIFF, IFF_TUN|IFF_NO_PI)` 绑定到指定设备名（`IFF_NO_PI` 表示不附加包信息头，直接裸 IP 包）。

## 2. 本仓库中的 tun/tap 设备与脚本

| 设备 | 类型 | 用途 | 网段（`etc/tunconfig`: `TUN_IP_PREFIX=169.254`） |
| --- | --- | --- | --- |
| `tun144` | tun（三层） | 自研 TCP/IP 栈端到端测试（测试方） | `169.254.144.1/24`（主机侧网关） |
| `tun145` | tun（三层） | 同上（参考实现方） | `169.254.145.1/24` |
| `tap10` | tap（二层） | `webget` 连真实互联网（走 ARP + NAT） | `169.254.10.1/24` |

配套脚本（需要 root）：

- `tun.sh <start|stop|restart|check> [tunnum...]`：创建/销毁/巡检 tun 设备。`start` 做的事：
  1. `ip tuntap add mode tun user <user> name tun144` 创建设备并归属当前用户（免 root 打开）；
  2. `ip addr add 169.254.144.1/24 dev tun144`、`ip link set dev tun144 up` 配置地址并启用；
  3. `ip route change ... rto_min 10ms` 调低 TCP 重传下限，保证测试定时器不被内核 RTO 干扰；
  4. iptables 给 `169.254.144.0/24` 打 connmark 并做 `MASQUERADE`（NAT），开放 FORWARD 链，`echo 1 > /proc/sys/net/ipv4/ip_forward` 开启内核转发。
- `tap.sh`：与 `tun.sh` 结构完全相同，只是 `mode tap`，并给 `tap10` 设 MAC 地址 `02:B0:1D:FA:CE:0a`。
- `etc/tunconfig`：只有一个变量 `TUN_IP_PREFIX=169.254`，两个脚本和 `txrx.sh` 共用。

## 3. tun 在 Lab 中的分工

CS144 的测试分三层，tun/tap 只在最外层（真实网络）出现：

| 层次 | 载体 | 涉及的 Lab | 测试手段 |
| --- | --- | --- | --- |
| 进程内模拟 | 无设备 | Lab 1-4（`fsm_*`、`recv_*`、`send_*`）、Lab 5（`arp_network_interface`）、Lab 6（`router_test`） | CTest 直接喂段/帧/包，不需要 sudo |
| **tun 端到端** | `tun144`/`tun145` | Lab 4 TCP 栈 | `tcp_ipv4`、`txrx.sh -i*`（`t_uc*/t_us*/t_ic*/t_is*`） |
| tap 端到端 | `tap10` | Lab 5 `NetworkInterface` + Lab 0 `webget` | `t_webget`（经主机 NAT 访问真实网站） |

**为什么 lab4 的端到端测试用 tun 而不是 tap？** tun 直接承载 IP 层，测试对象就是 Lab 2-4 实现的 `TCPConnection`（TCP 段 → IPv4 封装 → tun 写内核），不掺入 Lab 5 的 ARP/Ethernet 逻辑，方便定位问题；而 webget 要连真实公网服务器，必经二层网关 ARP 解析，所以走 tap10，正好顺带验证 Lab 5 的 `NetworkInterface`。

## 4. 数据通路

`tcp_ipv4` 测试的数据流（客户端在 tun144、参考实现在 tun145）：

```text
  tcp_ipv4(A)                    tcp_ipv4(B)
 ┌─────────────┐               ┌─────────────┐
 │ TCPConnection│               │ TCPConnection│
 │      │      │               │      ▲      │
 │ TunFdAdapter │               │ TunFdAdapter│
 └──────┬──────┘               └──────┬──────┘
        │ write(IPv4)                 │ read()
        ▼                             ▲
   ┌──────────┐   内核 IP 栈    ┌──────────┐
   │  tun144  │ ──路由/转发────▶ │  tun145  │
   └──────────┘  (ip_forward)   └──────────┘
```

相关代码：

- `libsponge/tcp_helpers/tuntap_adapter.hh:13-38` `TCPOverIPv4OverTunFdAdapter`：把 `TCPSegment` 用 `wrap_tcp_in_ip` 封装成 IPv4 包写入 tun；从 tun 读到的 IPv4 包用 `unwrap_tcp_in_ip` 解出段喂给 `TCPConnection`。这是 tun 与 TCP 栈之间的"翻译层"。
- `libsponge/util/tun.cc` `TunTapFD`：底层设备封装（`/dev/net/tun` + `TUNSETIFF`）。
- `apps/tcp_ipv4.cc`：`-d <tundev>` 选 tun 设备（默认 `tun144`），`-a <addr>` 设本机地址（默认 `169.254.144.9`），`-l` 监听模式。`txrx.sh` 里测试方固定用 `tun144`、参考方用 `tun145`（`REF_HOST=169.254.145.9`、`TEST_HOST=169.254.144.9`）。
- `apps/tun.cc`：调试工具，打开 `tun144` 循环读包并 hexdump + 解析 IPv4/TCP 头打印，可在两个 `tcp_ipv4` 对跑时旁观线上流量。

**它验证了什么？** tun 让自研用户态 TCP 栈与 Linux 内核协议栈真实对接：包进入内核后要经历路由查找、TTL 处理、转发，对端收包后校验和/序号全部真实走一遍——这是 `tcp_benchmark`（纯内存互喂）和 fsm 单元测试（人工构造段）覆盖不到的部分，也是 Lab 4 结课验收 `check_lab4` 的主要内容。

## 5. 为什么需要 sudo

tun/tap 的创建与网络配置都属于特权操作：

- `ip tuntap add`：创建设备节点；
- `ip addr add` / `ip link set up`：配置地址、启用设备；
- `iptables`（NAT/MASQUERADE、FORWARD 规则）：webget 上网必需；
- `/proc/sys/net/ipv4/ip_forward`：开启转发，tun144 ↔ tun145 的包才能穿过内核。

程序本身（打开已创建好的 tun 设备）不需要 root，所以 `tun.sh start` 创建时用 `user ${SUDO_USER}` 把设备归属给普通用户，测试进程就能免 root 打开设备。`tun.sh check` 则会幂等巡检（设备在、转发开、iptables 规则在），缺什么补什么——`check_lab4` 前会自动执行。

## 6. 常用命令

```bash
# 创建/巡检虚拟设备（缺则自动补建）
sudo ./tun.sh check 144 145
sudo ./tap.sh check 10

# Lab 4 完整测试（自动先跑 tun.sh check，再跑全部 t_* 测试，含 txrx.sh 端到端）
sudo cmake --build cmake-build-debug-wsl --target check_lab4

# 手动端到端：监听端 + 客户端
sudo ./cmake-build-debug-wsl/apps/tcp_ipv4 -l -d tun145 4000        # 参考方（或自研方）
sudo ./cmake-build-debug-wsl/apps/tcp_ipv4 -d tun144 169.254.145.9 4000

# 调试：旁观 tun144 上的原始 IP/TCP 包
sudo ./cmake-build-debug-wsl/apps/tun

# 清理
sudo ./tun.sh stop 144 145
```
