# retro-win3x — Windows 3.x / DOS 交叉开发环境

在 Linux 上用 Open Watcom 交叉编译 Win16 和 DOS 程序，放进 QEMU 里的 Windows 3.2（简体中文）/ 3.1 运行，
并通过串口和宿主机交互（控制代理、传文件、接本地 LLM 编码助手 pi 的聊天窗口）。

![Win3.2 桌面](docs/win32-desktop.png) ![聊天窗口](docs/chat.png)
![贪吃蛇](docs/snake.png) ![TUI](docs/tui.png)

## 目录
| 路径 | 内容 |
|---|---|
| `vm/run.sh` | 启动虚拟机：`vm/run.sh`（简体中文 Windows 3.2）或 `vm/run.sh win31`（英文 Windows 3.1） |
| `vm/build-images.sh` | 从 archive.org 下载原始系统盘，生成 `vm/images/` 里的镜像（装好代理、启动循环等） |
| `vm/images/` | 系统盘基础镜像（不在仓库里，由 `build-images.sh` 生成；运行时不会被写） |
| `vm/disks/` | 本机的工作盘（不提交）：首次运行时以基础镜像为底层自动创建；删掉即恢复初始状态 |
| `vm/tools/mon.sh` | QEMU monitor 小工具（`mon`、`typ`、`shot`），调试用 |
| `bin/` | 宿主机命令：`w31x` `w16ctl` `w16run` `dosrun` `w16new` `w16chatd` `setup-watcom` |
| `win16dev/` | Win16：`w16.mk` 公共规则，`template/` 模板，`hello/` `demo/` 示例，`chat/` LLM 聊天窗口，`tools/w16agent` 虚拟机里的代理，`tools/exitwin` |
| `dosdev/` | DOS：`dos.mk` 公共规则，`tui/` 文本界面 demo（16 位），`snake/` 320×200 贪吃蛇（32 位 DOS/4GW） |
| `docs/` | 截图 |

## 安装
```bash
sudo pacman -S qemu-desktop mtools socat imagemagick    # 或对应发行版的包
bin/setup-watcom                                       # 下载 Open Watcom v2 到 ~/opt/watcom
vm/build-images.sh                                     # 下载并生成系统镜像（约 30 MB 下载）
# 脚本和 Makefile 默认使用这些路径，用软链接指向仓库：
ln -s "$PWD/vm" ~/win31; ln -s "$PWD/win16dev" ~/win16dev; ln -s "$PWD/dosdev" ~/dosdev
for f in bin/*; do ln -s "$PWD/$f" ~/.local/bin/; done
vm/run.sh
```
聊天窗口还需要本地装好 [pi](https://github.com/badlogic/pi-mono) 编码助手。

**仓库不包含任何系统镜像。** `build-images.sh` 从 archive.org 下载：英文 Windows 3.1 + MS-DOS 6.22 预装盘（`win31vhd`）、
简体中文 Windows 3.2 已安装目录（`win32c`）。它们是仍受版权保护的旧商业软件，请只在自己有权使用的前提下下载。

## 命令（bin/）
| 命令 | 作用 |
|---|---|
| `w16new NAME` | 从模板建 Win16 项目 → `win16dev/NAME` |
| `make` / `make run` | 编译 / 编译+放进虚拟机+运行+截图（Win16 项目和 DOS 项目都一样） |
| `w16run X.EXE` | 运行 Win16 程序（自动关掉旧实例），截图 `w16run.png` |
| `dosrun X.EXE` | 运行 DOS 程序（全屏），截图 `dosrun.png`，然后发 Esc 退出；`-k` 保持运行，`-q "esc y"` 自定义退出键 |
| `w31x put/get/ls/rm/wipe` | 和 A 盘交换文件 |
| `w16ctl CMD` | 直接给代理发命令：PING TASKS RUN EXEC CLOSE COPY INI EXIT RESTART UPDATE VERSION |
| `w16ctl put/get/ls/rm/mkdir` | 通过串口和 Windows 交换文件（不经过软盘） |
| `setup-watcom` | 下载 Open Watcom v2 |

## 原理
- A 盘是软盘镜像：w31x 每次先通过 QEMU monitor 弹出软盘，用 mtools 读写，再插回，两边不会同时写。
- COM1 接到 `com1.sock`：W16AGENT.EXE（WIN.INI `load=` 自启动）在 Windows 里收命令、回 `OK`/`ERR`。
- AUTOEXEC.BAT 末尾有循环：`RESTART`/`UPDATE` 退出 Windows 后自动换新代理再进 Windows。
- 程序都复制到 `C:\W16RUN` 再运行：Win16 程序运行时会回读 exe；DOS 程序从软盘读会触发 Windows 的 DMA 缓冲区限制。

## DOS 目标
- `TARGET = dos16`：16 位实模式（wcc, small model）
- `TARGET = dos32`：32 位保护模式 DOS/4GW（wcc386），DOS4GW.EXE 自动一起拷过去

## 中文
- **默认系统已换成简体中文 Windows 3.2**：系统自带宋体/黑体和全拼、智能 ABC、五笔等输入法。
  Ctrl+Space 打开/关闭输入法，Ctrl+Shift 切换输入法；大写锁定开着时输入法不起作用。
- 英文 3.1 的盘也可以手动加装中文平台（可选，`build-images.sh` 不会装）：
- **RichWin 97**（Windows 下，archive.org `RichWin97`）：把 `RW97PRO` 目录拷进 C 盘后运行 `SETUP.EXE` 完整安装到 `C:\RW97PRO`，
  再 `w16ctl INI WIN.INI windows load 'C:\RW97PRO\RICHWIN.EXE C:\W16RUN\W16AGENT.EXE'` 让它随 Windows 启动。
  Alt+2 多元拼音、Alt+4 五笔、Alt+5 英汉、Alt+1 内码、Alt+0 回英文；Alt+\ 中/西文标点。
  拼音输完后数字键选字，`=`/`-` 翻页；大写锁定开着时输入法不起作用。
- **UCDOS 7.0**（纯 DOS 下，archive.org `ucdos7`）：把镜像里的 `UCDOS` 目录拷到 `C:\UCDOS`，在 DOS 下运行 `C:\UCDOS\UCDOS`
  （可在 CONFIG.SYS 里加 `[MENU]` 开机菜单）。
- 文本用 GB2312/GBK 编码：`iconv -f utf-8 -t gbk a.txt > A.TXT && w16ctl put A.TXT`
- 串口传文件：`w16ctl put/get/ls/rm/mkdir`（put ~20 KB/s，get ~4 KB/s）

## Pi 聊天窗口（CHAT.EXE）
- 源码 `win16dev/chat/`（UTF-8 源码，`GBK = 1` 编译时自动转 GBK 并加 `-zk1`），`make run` 发进虚拟机运行。
- 链路：CHAT.EXE ⇄ COM2 ⇄ `vm/com2.sock` ⇄ `w16chatd` ⇄ `pi -p --mode json --session-id <会话>`。
- `run.sh` 会自动在后台启动 `w16chatd`（日志 `vm/w16chatd.log`），pi 的工作目录是 `~/w16chat`。
  想换目录或给 pi 加参数：`w16chatd --cwd DIR -- --model xxx --no-tools`。
- 窗口里 Enter 发送、Ctrl+Enter 换行；回复中按钮变“停止”；“新对话”开新的 pi 会话。
- 协议（每行一条，内容是 GBK 的 base64）：Win→Linux `HELLO`/`MSG`/`NEW`/`STOP`；Linux→Win `B` 开始、`T` 文本、`E` 结束、`S` 状态（英文）、`X` 错误、`I` 标题信息（`agent|model`，窗口标题显示为 `LLM - Pi - 模型名`）。

## 中文源码
Win16 项目的 Makefile 里加 `GBK = 1`：源码用 UTF-8 写中文，编译前自动转成 GBK，并给 wcc/wrc 加 `-zk1`
（中文双字节支持，避免 GBK 第二字节 0x5C 被当成反斜杠）。见 `win16dev/chat/`。
