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

