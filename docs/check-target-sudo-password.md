# `check` target 请求 sudo 密码的原因与复现方法

触发密码输入的不是 CMake，而是项目的网络初始化脚本：

```text
check
 ├─ tun.sh check 144 145
 ├─ tap.sh check 10
 └─ ctest ...
```

`tun.sh` 和 `tap.sh` 需要创建 TUN/TAP 虚拟网卡、修改路由和 iptables，因此会执行 `/usr/bin/sudo`。

相关代码位置：

- `etc/tests.cmake:234`：定义 `check` target。
- `tun.sh:55`：检查并切换到 sudo 执行。
- `tap.sh:57`：检查并切换到 sudo 执行。

## 最小复现

```bash
cd /home/wdidada/git/cpp/CS144-libsponge

# 清除 sudo 凭据缓存
sudo -k

# 强制进入 tun.sh 的 sudo 分支
./tun.sh start 144 145
```

此时应出现：

```text
[sudo] password for wdidada:
```

如果只想观察提示、不想真的创建网卡，可在提示出现后按 `Ctrl+C`。

## 复现 `check` 原本的行为

```bash
sudo -k
./tun.sh check 144 145
./tap.sh check 10
```

注意：如果 `tun144`、`tun145` 和 `tap10` 已经创建且状态正常，`check` 会直接成功，不再请求密码。

## 查看 CMake 最终展开的命令

以下命令只显示命令，不执行测试：

```bash
ninja -C cmake-build-debug-wsl -t commands check
```

展开结果为：

```bash
./tun.sh check 144 145 &&
./tap.sh check 10 &&
ctest --output-on-failure --timeout 10 -R '^t_|^arp_|^router_'
```

由于 `tun.sh` 排在最前面，这次提示通常首先来自它；如果 TUN 已存在而 TAP 不存在，则提示来自 `tap.sh`。
