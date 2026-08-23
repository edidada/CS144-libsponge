# Git 分支用途与提交合并情况

本文记录仓库各分支的用途、提交关系和合并状态。数据基于 **2026-08-24** 的本地仓库；后续新增提交后，哈希和领先数量可能变化。

## 当前结论

- 当前检出分支是 `solution`，提交为 `d62e1aa`。
- `master` 和全部 `labN-startercode` 分支均已合入 `solution`。
- `solution` 是包含 Lab 0～6 实现及当前 Linux/WSL CI 修复的完整解答分支。
- `solution_mac` 是从完整解答较早版本分出的 macOS 适配分支，尚未合回 `solution`。
- 所有本地分支都与对应的 `origin/*` 分支同步，没有本地独有或尚未拉取的提交。
- `origin/HEAD` 指向 `origin/solution`，因此远程仓库的默认分支是 `solution`。
- `sandisks/solution`、`origin/solution` 和本地 `solution` 当前均指向同一个提交 `d62e1aa`。

## 分支拓扑概览

starter code 的课程发布主线大致如下：

```text
master (Lab 0 starter)
  └─ lab1-startercode
      └─ lab2-startercode
          └─ Lab 3 assignment 基点 35c1f1d
              ├─ lab3-startercode（额外包含 send_window 测试修复）
              └─ lab4-startercode
                  └─ lab5-startercode
                      └─ lab6-startercode
```

`solution` 在自己的开发线上逐阶段合入以上 starter code，然后完成对应 Lab 的实现：

```text
Lab 0 实现
  └─ 合入 Lab 1 starter → Lab 1 实现
      └─ 合入 Lab 2 starter → Lab 2 实现
          └─ 合入 Lab 3 starter → Lab 3 实现
              └─ 合入 Lab 4 starter → Lab 4 实现
                  └─ 合入 Lab 5 starter → Lab 5 实现
                      └─ 合入 Lab 6 starter → Lab 6 实现
                          └─ README（566e2b6）
                              ├─ solution：现代工具链与 Linux/WSL CI 修复
                              └─ solution_mac：macOS、旧 CI 和文档相关改动
```

需要注意，`lab3-startercode` 和 `lab4-startercode` 的 tip 不是简单的父子关系：二者都从 Lab 3 assignment 提交 `35c1f1d` 延伸。`lab3-startercode` 额外带有测试修复 `217e1bb`，而 `lab4-startercode` 从 `35c1f1d` 直接加入 Lab 4 框架。`solution` 分别合入了这两个分支，所以两边内容都已包含。

## 各分支用途和状态

下表中“`solution` 领先”表示该分支的全部提交均已包含在 `solution` 中，数字是 `solution` 在 Git 提交图上额外拥有的提交数。

| 分支 | 当前 tip | 用途 | 相对 `solution` 的状态 |
|---|---:|---|---|
| `master` | `337e97d` | CS144 Lab 0 初始框架；主要任务包括网络热身、`webget` 和 `ByteStream` | 已完全合入；`solution` 领先 47 个提交 |
| `lab1-startercode` | `ce0db30` | Lab 1 框架，加入 `StreamReassembler` 接口、测试和 writeup | 已完全合入；`solution` 领先 46 个提交 |
| `lab2-startercode` | `7be25cf` | Lab 2 框架，加入 `WrappingInt32`、TCP segment/receiver 及相关测试 | 已完全合入；`solution` 领先 43 个提交 |
| `lab3-startercode` | `217e1bb` | Lab 3 框架，加入 `TCPSender`、发送端测试及一个 sanitizer 测试修复 | 已完全合入；`solution` 领先 41 个提交 |
| `lab4-startercode` | `0e18b33` | Lab 4 框架，加入 `TCPConnection`、TUN 适配、端到端应用和测试脚本 | 已完全合入；`solution` 领先 41 个提交 |
| `lab5-startercode` | `2d51f65` | Lab 5 框架，加入 Ethernet/ARP `NetworkInterface`、TAP 适配和测试 | 已完全合入；`solution` 领先 39 个提交 |
| `lab6-startercode` | `d598057` | Lab 6 框架，加入 IPv4 router、最长前缀匹配相关接口和网络模拟器 | 已完全合入；`solution` 领先 38 个提交 |
| `solution` | `d62e1aa` | Lab 0～6 完整实现；当前默认开发分支，并包含现代编译器、GitHub Actions、TUN/TAP 测试修复 | 基准分支 |
| `solution_mac` | `3ae5ee7` | 完整解答的 macOS 兼容分支；包含 macOS 编译适配、旧 CI 配置和生成的 HTML 文档等 | **未合入**；自身独有 15 个提交，`solution` 独有 6 个提交 |

这些 `startercode` 分支主要用于查看某一 Lab 开始时课程提供了哪些代码，不应误认为各 Lab 的完成答案。完成答案集中在 `solution`。

## `solution` 中的 starter code 合并记录

| 合入内容 | merge commit | 日期 | 随后的主要实现节点 |
|---|---:|---:|---|
| `lab1-startercode` | `1fe5b58` | 2020-08-24 | `131c535`：Lab 1 测试通过 |
| `lab2-startercode` | `61ba380` | 2020-08-25 | `4df37e3`：WrappingInt32；`a8364df`：Lab 2 通过 |
| `lab3-startercode` | `0829cac` | 2020-08-29 | `95b57c1`：Lab 3 通过 |
| `lab4-startercode` | `ee87ff2` | 2020-09-01 | `70629d3`～`b845b8c`：TCPConnection 实现及 Lab 4 修复 |
| `lab5-startercode` | `85d61bf` | 2020-09-17 | `646d8cc`：Lab 5 通过 |
| `lab6-startercode` | `707456c` | 2020-09-18 | `888faff`：Lab 6 通过 |

Lab 0 没有单独的合并提交，因为 `solution` 直接从 `master` 的 Lab 0 starter 开始开发；`760c911` 是 Lab 0 完成并通过测试的提交。

## `solution` 与 `solution_mac` 的分叉

两条分支的最近共同祖先是：

```text
566e2b6  2020-09-18  Add: README
```

从该提交之后，二者独立演进。

### `solution` 独有的 6 个提交

```text
220e299  修复编码和换行、适配现代工具链、加入 GitHub Actions CI
a2591f1  串行执行 TUN/TAP 端到端测试
d51f426  CI 加入 Lab 0～3 检查及 webget/tap10 测试
fa56e7e  ignore
1b25778  诊断 webget/tap10 路径并提高超时
d62e1aa  处理不可达的课程网站并确保 tap10 启用
```

### `solution_mac` 独有的 15 个提交

`solution_mac` 包含 15 个在 `solution` 提交图中不存在的提交。主要内容包括：

- macOS 编译兼容调整；
- 较早的 Ubuntu 20.04 和单平台 CI 配置；
- UTF-8、`.gitignore` 等维护改动；
- 一批生成的 Doxygen HTML 文档；
- 若干提交信息仅为 `add`/`ci` 的历史提交。

最新提交 `3ae5ee7` 明确用于 macOS 编译适配，修改了 CMake、socket/TUN、字节序及部分类型兼容代码。

“未合入”是按 Git 祖先关系得出的结论：即使两条分支中存在功能相似的改动，它们仍然是独立提交。若要让 macOS 分支获得当前 `solution` 的 6 个新提交，需要显式 merge、rebase 或 cherry-pick，并处理 CI 和跨平台代码可能产生的冲突。

## 本地分支与远程分支

仓库配置了两个远程：

```text
origin    git@gitee.com:edidada/CS144-libsponge.git
sandisks  git@github-sandisks:sandisks/CS144-libsponge.git
```

`origin` 保存了所有本地分支的对应远程分支。`sandisks` 当前只显示 `sandisks/solution`，并且与 `origin/solution` 完全相同。`remotes/origin/*` 只是远程跟踪引用，不是另一套需要额外合并的代码。

## 建议如何使用

- 在 Linux/WSL 上继续开发、运行完整测试或查看最终答案：使用 `solution`。
- 查看某个 Lab 开始时的原始接口和测试：切换到对应的 `labN-startercode`，但不要在那里寻找最终实现。
- 在 macOS 上研究兼容改动：查看 `solution_mac`；同时注意它尚未包含 `solution` 最近 6 个 CI/工具链提交。
- `master` 在此仓库中是历史 Lab 0 starter，不是最新主线；不要根据名字默认它包含最新代码。

## 重新检查分支状态的命令

```bash
# 查看本地及远程分支、跟踪关系和 tip
git branch -a -vv

# 查看整体提交拓扑
git log --all --graph --decorate --oneline --date-order

# 查看哪些本地分支已合入 solution
git branch --merged solution
git branch --no-merged solution

# 比较某个分支和 solution 各自独有的提交数量
git rev-list --left-right --count solution_mac...solution

# 查看双方独有的具体提交
git log --oneline solution_mac..solution
git log --oneline solution..solution_mac

# 判断一个分支是否已完整包含在 solution 中；返回码 0 表示已包含
git merge-base --is-ancestor lab6-startercode solution
```
