本文为我的斯坦福计算机网络课的编程实验（Lab Assignments）的完成后的作业代码。
课程全称：CS 144: Introduction to Computer Networking。

所有LAB完成后的代码放在solution分支下，最开始的LAB0的原始代码放在master分支下，请根据自己的需求自行切换。

如果因为配置问题或者别的问题卡关了可以参考我的笔记：[https://www.cnblogs.com/kangyupl/p/stanford_cs144_labs.html](https://www.cnblogs.com/kangyupl/p/stanford_cs144_labs.html)

lab0

Writing webget
要求实现get_URL函数，功能为向指定IP地址发送HTTP GET请求，然后输出所有响应。可参考配套Doc中TCPSocket的示例代码。此外多读读讲义提示，注意下EOF和shutdown()的参数即可。 
webget.cc
void get_URL(const string &host, const string &path)

An in-memory reliable byte stream
要求实现一个有序字节流类（in-order byte stream），使之支持读写、容量控制。这个字节流类似于一个带容量的队列，从一头读，从另一头写。当流中的数据达到容量上限时，便无法再写入新的数据。特别的，写操作被分为了peek和pop两步。peek为从头部开始读取指定数量的字节，pop为弹出指定数量的字节。

第一反应是搞个循环队列，容器基于数组，长度等于容量，这样内存被充分利用，效率也不错。不过讲义要求我们用“Modern C++”，避免用普通指针，所以我退而求其次用std::deque代替。为什么不用std::queue？因为queue只能访问开头的节点，无法实现peek操作。

byte_stream.hh


LAB1
要求实现一个流重组器（stream reassembler），可以将带索引的字节流碎片重组成有序的字节流。每个字节流碎片都通过索引、长度、内容三要素进行描述。重组完的字节流应当被送入指定的字节流（byte stream）对象_output中。

特别注意：

0.这节需要安装pcap库和pcap-dev库才能正常编译，如果没编译没报错那就没事了。

1.碎片可能交叉或重叠。

2.如果某次新碎片到达后字节流的开头部分被凑齐，那就应当立刻把凑齐的部分立刻写入到_output中。即对应讲义中的:

When should bytes be written to the stream?

As soon as possible. The only situation in which a byte should not be in the stream is that when there is a byte before it that has not been “pushed” yet.

3.碎片可能是一个只包含EOF标志的空串

4.LAB0的顺序字节流和LAB1的流重组器各有各的容量限制。流重组器把字节流写满后，只有当字节流腾出空后才能继续写，相当于字节流满时流重组器出口被“堵住”了。同样当流重组器容量满了后自身也无法被写入新数据，此时到来的新碎片只能被丢弃掉。

第一反应联想到了操作系统里的进程内存管理，用一个二叉排序树来记录每个碎片的索引、长度，排序规则为按索引值升序，每次插入新碎片时判断能不能和前后碎片进行合并。流的内容则可以用一个数组来做缓冲区，或者干脆一块存储在二叉树的节点里。不过还是因为“Modern C++”的缘故，我再次退而求其次用std::list代替之。等我哼哧哼哧花了好几个小时写完LAB1后，又哼哧哼哧得改了一众BUG后，才想起std::set底层就是用红黑树实现的，可以直接拿来用。

最终实现与上文愿景差不多，用一个block_node结构体来存放每个碎片的索引、长度、内容。又因为set排序实现基于对应节点类型的小于运算符规则，所以我把block_node结构体的小于运算符重载为按索引值升序。再简单说下我的push_substring处理流程：

容量判断：满了就立刻返回。
处理子串的冗余前缀：如果子串包含已经被写入字节流的部分，就把这部分剪掉。
合并子串：运用set自带的lowerbound快速确定插入位置，前后重复比较，用个自己写的子函数判断重叠的字顺便合并之。
写入字节流：如果流重组器头部非空，就把头部写入字节流，并更新指示头部的游标。

stream_reassembler.hh

stream_reassembler.cc

---

LAB2

要求实现序列号（Sequence Numbers）功能，完成三种序号之间的转换：序列号 seqno、绝对序列号 absolute seqno、流索引 stream index。三者区别如下：

|  | Sequence Numbers | Absolute Sequence Numbers | Stream Indices |
| --- | --- | --- | --- |
| 起点 | Start at the ISN | Start at 0 | Start at 0 |
| 是否包含 SYN/FIN | Include SYN/FIN | Include SYN/FIN | Omit SYN/FIN |
| 位数/是否回绕 | 32 bits, wrapping | 64 bits, non-wrapping | 64 bits, non-wrapping |

需要重点理解 `checkpoint` 的作用：它表示上一次转换求得的 absolute seqno，本次转换出的 absolute seqno 应当选择与 checkpoint 最为接近的那一个。原理是虽然 segment 不一定按序到达，但相邻两个 segment 序号差值几乎不可能超过 INT32_MAX（除非延迟以年为单位或发生比特差错）。实际操作就是把算出的序号分别加减 `1ul << 32` 后与 checkpoint 比较，取差的绝对值最小的那个。

wrapping_integers.hh

wrapping_integers.cc

Implementing the TCP receiver

要求实现基于滑动窗口的 TCP 接收端 TCPReceiver。整个接收端空间由窗口空间（基于 StreamReassembler）和缓冲区空间（基于 ByteStream）共享，需要注意**窗口长度等于接收端容量减去还留在缓冲区的字节数**（即 `_capacity - buffer_size`），只有当字节从缓冲区读出后窗口长度才能缩减。

实现要点：

0. 在收到 SYN 之前拒绝一切数据段，首个 SYN 段记录 ISN。
1. 重复的 SYN / FIN 一律拒绝。
2. ackno 的值为期望收到的下一个字节的序号（含 SYN/FIN 计数），未收到 SYN 时不返回 ackno。
3. 完全落在窗口之外的段直接丢弃。

tcp_receiver.hh

tcp_receiver.cc

LAB3

要求实现 TCP 发送方 TCPSender。三个容易困惑的点：

0. 本 LAB 用的是基于**累计确认**的 ARQ 协议：收到一个 ackno 代表接收方已收到 ackno 之前的所有段，与课本图 3-33 里"分别确认"的协议不同。实现上用队列保存未确认段，重传时只传队头即可。
1. 首个发出的段应是**只包含 SYN 的段**，用作第一次握手（课本这么讲，但讲义里没说，容易写错）。
2. 计时器启动的时机：发送新段时若计时器未在运行则启动；收到合法 ack 且仍有未确认段时重启；超时重传后 RTO 翻倍。

tcp_sender.hh

tcp_sender.cc

LAB4

要求实现 TCPConnection，把 TCPSender 和 TCPReceiver 封装成完整的 TCP 有限状态机（FSM，涉及 12 种状态转换）。难点主要在状态转换的细节逻辑，且 LAB4 之前的测试样例并不全面，前面几个 LAB 的潜在 BUG 会在本 LAB 的完备测试中集中爆发，需要回头修改前面的代码。

tun.cc 编译错误：若报错 `field 'ifru_addr' has incomplete type 'sockaddr'`，参考 osquery issue #277，在 `libsponge/util/tun.cc` 中添加 `#include <sys/socket.h>` 即可。

make check 超时或随机报错：webget 测试网站服务器在国外（网络问题），多重试几次即可。

tcp_connection.hh

tcp_connection.cc

性能结果（作者 WSL i5-6200U / 阿里云 E5-2682 v4）：

```
$ ./apps/tcp_benchmark
CPU-limited throughput                : 0.67 Gbit/s
CPU-limited throughput with reordering: 0.52 Gbit/s
```

已超过讲义要求的最低 0.1 Gbit/s。

瓶颈优化

讲义只要求最低 0.1 Gbit/s，但示例是 1.78 Gbit/s，故利用《深入理解计算机系统》中的性能优化知识进一步优化。用 gprof 定位到 ByteStream 的 `write`/`pop`/`peek` 共同占据约 80% 运行时间，于是改用 `BufferList` 作为 ByteStream 的容器，将基于**内存拷贝**的存储改为基于**内存所有权转移**（std::move）的存储。

优化后（作者机器）：

```
$ ./apps/tcp_benchmark
CPU-limited throughput                : 1.84 Gbit/s
CPU-limited throughput with reordering: 0.64 Gbit/s
```

byte_stream.hh

byte_stream.cc

LAB5

要求实现 ARP 协议，在网络层与链路层之间搭桥。需要注意讲义没提到的一点：发送 ARP request 后若没有响应要**五秒后重发**，且在上一个请求被正常响应之前，其他的请求都要排在后面。

network_interface.hh

network_interface.cc

LAB6

要求实现基于最长前缀匹配（Longest Prefix Match）规则的路由器转发功能。讲义说明 O(N) 复杂度可以接受，故直接用 vector 作为路由转发表即可。注意当 `next_hop` 为空时代表本路由器就是目的路由器，此时 `next_hop` 取 dgram 的目的 IP 地址。

router.hh

router.cc

至此，CS144 所有 LAB 均已完成。

