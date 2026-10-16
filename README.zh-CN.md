# mdt — 终端里的 Markdown 阅读器

[![build](https://github.com/wudream813/markdown-in-terminal/actions/workflows/build.yml/badge.svg)](https://github.com/wudream813/markdown-in-terminal/actions/workflows/build.yml)
[![release](https://img.shields.io/github/v/release/wudream813/markdown-in-terminal)](https://github.com/wudream813/markdown-in-terminal/releases/latest)
[![license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

[English](README.md) · **中文说明**（对应 v0.3.8）

用 C++17 写的**终端 Markdown 阅读器**，通过终端的**图形协议**（kitty / iTerm2 /
sixel）绘制公式和图片，内置**兼容 KaTeX 语法的数学排版引擎**。TeX 排版、SVG
光栅化、图片解码、三种图形协议全部编进**一个免依赖的二进制文件**里。

![mdt 阅读 demo.md：行内公式、嵌套列表、代码框](demo/screenshot.png)

```
  ┌─ demo.md ───────────────────────────────────────────────────────────────┐
  │  ∫_{-∞}^{∞} e^{-x²} dx = √π          # 由内置引擎排版                  │
  └─────────────────────────────────────────────────────────────────────────┘
```

更多截图：[带折叠框和图片的洛谷题解](docs/luogu-render.png) ·
[合并单元格表格](docs/table-render.png) ·
[`pmatrix` 行间公式](docs/matrix-render.png) ·
[嵌套引用](docs/nested-quotes.png) ·
[语法高亮代码](docs/code-render.png) ·
[Windows 上同一个阅读器](demo/windows.png)。

## 亮点

* **KaTeX 语法，零运行时依赖。** 数学管线是
  TeX/KaTeX 源码 → SVG（MathJax 的 TeX 输入 + SVG 输出，约 1.8 MB JS 内嵌在二进制里）
  → 由手写的 SVG 光栅化器转成 PNG。JS 由内嵌的 **QuickJS** 执行，所以一个 `mdt`
  可执行文件就能渲染 `\frac`、`\sum`、`\int`、`\begin{pmatrix}` ……不需要 node、
  不需要浏览器、不需要任何外部文件。
* **三种图形协议**，启动时自动探测：kitty（`\x1b_G…`，也支持 WezTerm/Ghostty/Konsole）、
  iTerm2 内联图片（`\x1b]1337;File=`）、sixel（xterm/mlterm/foot/Windows Terminal）。
* **行内图形**：公式和图片坐在文字基线上（kitty 支持亚单元格偏移，其它协议对齐到单元格），
  随段落一起流动。
* **优雅降级**：终端没有图形协议时，公式会转写成 Unicode（`∑`、`∫`、`√`、上下标），
  而不是直接消失；按 `m` 可以在排版公式和纯文本公式之间随时切换。
* **洛谷方言**：在 GFM 之上支持洛谷文章编辑器的 remark-directive 语法 ——
  `:::info` / `:::success` / `:::warning` / `:::error` 提示框（彩色竖条、`[...]` 加粗标题、
  用更长的冒号围栏嵌套、可点击标题折叠）、`:::align{left|center|right}`、
  Codeforces 风格 `:::epigraph[——作者]`、`::cute-table{three}` / `::cute-table{tuack[=N]}`
  表格样式、`^`/`<`/`>` 单元格合并、```` ```cpp lines=5-6,11 ```` 代码行高亮，以及
  「没有语言标记的代码块按 C++ 处理」（`plain`/`plaintext` 关闭高亮）等洛谷约定。
  不使用指令的文档行为不变。
* **对生成器产出的 Markdown 宽容**：从「复制为 Markdown」和 CMS 导出的文件往往整篇被
  HTML 转义（`&#35;` 代表 `#`）或被反斜杠转义（`\#`、`\*`），mdt 可以按需修复
  （`--entities=force` / `--escapes`），并且**绝不误改公式和代码**。文件里的 HTML
  （`<p>`、`<h2>`、`<ul><li>`、`<table>`、`<pre>` 及行内标签）会转换成对应的 Markdown；
  像 `<that>`、`<T>`、`a < b` 这样的正文占位符保持原样。
* **引用可以嵌套**：`>`、`> >`、`> > >`……每一层都有自己的竖条和两列缩进，层内可以是
  段落、代码、列表、表格或另一层引用；换行后的每一行都会带上所有竖条。
* **Windows 文件名支持 Unicode**：命令行从 `GetCommandLineW()` 重建，路径一律走
  `_wfopen`/`GetFileAttributesW`，控制台代码页切到 UTF-8，所以 `mdt 中文.md` 正常。
* **图片不阻塞阅读**：`![](https://…)` 在后台线程下载，首帧几百毫秒就出来，
  滚动和按键全程可用，图片到达后自动刷新布局。按 `R` 可以重新下载。
  没有图形协议的终端根本不会去联网。
* **公式始终清晰**：公式按终端精确的单元格网格（`cols × cell_w` × `rows × cell_h` 像素）
  光栅化并 1:1 放置，基线对齐到文字基线；表格里的公式也是同样处理。
* **紧凑的帧输出**：只发送发生变化的单元格，一次光标跳转一个连续区段、一次 SGR
  一类样式（一个标题一个转义序列，而不是每个字符一个），没有变化的内容不重绘。
* **退出后终端可用**：正常退出、`kill`、Ctrl-C/Ctrl-Break、关闭控制台窗口，都会把
  备用屏幕、鼠标上报、光标和终端模式（termios / `SetConsoleMode`）恢复原状。
* 运行时探测终端能力：单元格像素尺寸（`CSI 16 t`）、窗口尺寸（`CSI 14 t`）、sixel
  支持（DA1）、kitty 图形支持（`\x1b_Gi=…,a=q`），字号比例跟随用户真实字体。

## 安装

预编译二进制在 [releases 页面](https://github.com/wudream813/markdown-in-terminal/releases/latest)，一个文件，不用装别的东西。

| 平台 | 文件 | 说明 |
|:--|:--|:--|
| Linux x86-64 | `mdt-linux-amd64` | 0.3.9 起为静态链接 —— 任何发行版都能直接跑，不再挑 glibc 版本 |
| Linux arm64 | `mdt-linux-arm64` | 同样静态链接：树莓派 4/5、ARM 服务器、Apple 芯片上的 Linux 虚拟机都能用 |
| macOS Apple 芯片 | `mdt-macos-arm64` | 未签名未公证，下载后执行一次 `xattr -d com.apple.quarantine mdt` |
| Windows x64 | `mdt-win64.exe` | 或者 `mdt-<版本>-win64.zip`（里面还带 demo） |

```sh
# Linux（x86-64 或 arm64）与 macOS（Apple 芯片）一行装好，不需要包管理器：
# install.sh 会自己挑本机对应的那个文件
curl -fsSL https://raw.githubusercontent.com/wudream813/markdown-in-terminal/main/install.sh | sh

# 等价的手动做法：
curl -fL -o mdt https://github.com/wudream813/markdown-in-terminal/releases/latest/download/mdt-linux-amd64
chmod +x mdt && sudo install -m 0755 mdt /usr/local/bin/mdt
mdt --list-caps          # 看看探测到的单元格尺寸、图形协议、字体
mdt 文档.md

# Linux arm64（树莓派、ARM 服务器等）
curl -fL -o mdt https://github.com/wudream813/markdown-in-terminal/releases/latest/download/mdt-linux-arm64
chmod +x mdt && sudo install -m 0755 mdt /usr/local/bin/mdt

# macOS（Apple 芯片）
curl -fL -o mdt https://github.com/wudream813/markdown-in-terminal/releases/latest/download/mdt-macos-arm64
chmod +x mdt && xattr -d com.apple.quarantine mdt 2>/dev/null
sudo install -m 0755 mdt /usr/local/bin/mdt
```

也可以交给包管理器（升级时 `mdt --version` 会告诉你新版号）：

```sh
# macOS（Apple 芯片）与 Linux（x86-64 或 arm64）
brew tap wudream813/mdt && brew install mdt
```

```powershell
# Windows
scoop bucket add wudream813 https://github.com/wudream813/scoop-bucket
scoop install wudream813/mdt
```

每个 release 除二进制外还附 `install.sh`、`LICENSE` 和 `SHA256SUMS`，下载后不必只信页面本身。

在源码目录里 `make install` 还会装 man 手册和补全脚本：

```sh
make && sudo make install      # 默认 PREFIX=/usr/local
man mdt                        # 同样的选项，trofi 格式
```

mdt 不往用户目录写任何东西（没有配置文件、没有缓存）；除非文档里真的有
`![](https://…)` 图片，否则它不会访问网络。

## 从源码构建

```sh
make -j            # 生成 build/mdt（需要 C11 + C++17 编译器）
# 或者
cmake -B build && cmake --build build -j
```

Linux 和 macOS 用同一份源码（`src/platform.cpp` 的 POSIX 分支；macOS 上会在
`/System/Library/Fonts` 找 Menlo/Monaco、在
`/System/Library/Fonts/Supplemental` 找 PingFang）。除了编译器和 `make`，不需要安装任何东西 ——
其它依赖都已内置：`stb_truetype/stb_image/stb_image_write/stb_image_resize2` 和 QuickJS-NG。
只有改动 JS 包时才需要 `python3`（`make assets`）。如果有 libcurl 会被链接进来用于下载远程图片，
否则回退到 `curl` 命令（两者都不会把 URL 交给 shell）。

### 为 Windows 构建（MinGW-w64 交叉编译）

```sh
sudo apt install mingw-w64          # 或 dnf install mingw64-gcc-c++; brew install mingw-w64

make windows                        # -> build/win/mdt.exe         (x86-64)
make windows32                      # -> build/win32/mdt.exe       (32 位)
sh tools/build_windows.sh           # 构建 + 依赖检查 + dist/*.zip
```

### 为 Linux arm64 构建（交叉编译）

arm64 的发布文件就是同一套静态构建交叉编出来的——不需要 ARM 机器，
x86-64 上用 `qemu-user-static` 就能直接跑：

```sh
sudo apt install g++-aarch64-linux-gnu qemu-user-static   # 后者用来运行

make arm64                          # -> build/arm64/mdt（静态、AArch64）
./build/arm64/mdt demo/demo.md      # 在 x86-64 上经 binfmt_misc/qemu 直接跑
```

`.exe` 用 `-static -static-libgcc -static-libstdc++` 链接，只依赖 `KERNEL32.dll` 和
`msvcrt.dll`，没有需要一起分发的 MinGW 运行库。Windows 上远程图片走系统自带的
**WinINet**（不需要 curl.exe，也没有 shell）。字体从 `%WINDIR%\Fonts` 里找
（正文用 Consolas/Cascadia Mono，宽字符用 MS YaHei / SimSun / Noto CJK）。

### 直接在 MSYS2 里构建

两个 MSYS2 shell 都能用；`make` 不是靠编译器叫什么名字来判断分支的，而是直接问它
目标平台（`g++ -dumpmachine`）—— 因为在 MSYS2 里 MinGW 那套编译器也叫 `g++`。

```sh
# MSYSTEM=MINGW64 / UCRT64 / CLANG64 —— 真正的 Windows 程序
pacman -S mingw-w64-x86_64-gcc make
make -j                            # -> build/mdt.exe，参数与 `make windows` 相同

# MSYSTEM=MSYS —— 给 MSYS shell 用的 POSIX 版本（/c/… 这类路径直接可用）
pacman -S gcc make                 # 在“MSYS2 MSYS”终端里
make -j                            # -> build/mdt.exe
```

在 MSYS shell 里目标平台是 `x86_64-pc-msys`，所以 mdt 走 POSIX 那一半，两处差别：
`-std=c++17`/`-std=c11` 换成 `-std=gnu++17`/`-std=gnu11`（严格 ANSI 模式会把
`strdup`、`realpath` 之类藏在 `__STRICT_ANSI__` 后面，不放出来就根本编译不过），
以及 iconv 要链 `-lconv` 而不是 `-liconv`。这两类库都是**探测**出来的而不是写死的，
所以你的工具链换了名字或没有那个 stub，也照样能链接。

### Windows 下的键盘与鼠标

Windows 的控制台有两套读输入的方式，mdt 用的是**按记录读**（`ReadConsoleInputW`），
而不是从控制台句柄里读字节流。字节流那条路看起来更简单，但微软自己的文档写得很直白：
对着控制台 `ReadFile`「不会返回功能键或方向键的键盘事件」，鼠标、窗口、焦点事件
「会被丢弃」—— 就算打开了 `ENABLE_MOUSE_INPUT`，这些事件也只有 `ReadConsoleInput`
读得回来。而且这层翻译是宿主（`CONIN$`/ConPTY）做的，宿主之间并不一致：终端一口气发
出的序列，到了控制台程序这儿可能变成好几条独立记录，于是程序手里就剩一个孤零零的
`ESC` —— 在 mdt 里那正是「退出」，没有报错、退出码 0。issue #3 报告的 WezTerm 下的
现象就是这么来的。

所以 mdt 现在直接读控制台真实记下的东西：`KEY_EVENT` 会翻译成终端本来会发出的转义序列
（方向键、Home/End、PgUp/PgDn、Insert/Delete、F1–F12，带 Shift/Ctrl 修饰；而本身就
带字符的 `KEY_EVENT` —— 字母、回车、Tab、退格、Esc、Ctrl-C —— 原样透传），
`MOUSE_EVENT` 会翻译成同样坐标的 SGR 鼠标报码。两者都是一整条交给解析器，所以任何
按键或点击都不可能再以「一个裸 ESC」的形式到达。控制台也不存在「输入结束」：
你把文档管道给 `mdt -` 时 stdin 不是控制台，走的还是原来的字节路径。

记录模式位也要说清楚：请求归请求，控制台让不让是另一回事。mdt 现在记的是控制台**实际保留**的
那组模式位，并在 key log 里写一行 `console modes: asked in=0x… kept in=0x… (mouse events on/REFUSED)`。
另外 mdt 不再相信「句柄可读就等于有输入」这种说法（控制台缓冲区为空时它照样自称可读，事件循环
会因此空转），而是直接看缓冲区里有多少条记录；窗口尺寸事件也不再申请（mdt 自己轮询尺寸）。
空闲时它一帧都不画，也一个字节都不写。

图片是同一件事的另一面：**Windows 上默认不画图**。控制台宿主（ConPTY）对它看不懂的转义
序列不是原样透传，而是当成要显示的文本、按控制台宽度折行。用 mdt 自己实测：一条 SGR 鼠标
报码回来时被插了一个 CR，kitty 的探测 APC 插了两个，一张 2.7 KB 的 sixel 图插了 64 个，
iTerm2 的 OSC 更是整段 base64 被当成文本铺在屏幕上（issue #3 里 wezterm 的现象）。
所以 Windows 上 mdt 只在**终端自己应答过能力探测**时才画图；仅仅「看起来支持」
（`TERM_PROGRAM=WezTerm`、`KITTY_WINDOW_ID` 等）的一律改用 Unicode 文字公式。
`--gfx=kitty|sixel|iterm2` 仍可强制 —— 知道自己的宿主会透传序列时用它，mintty 就是这一类
（MSYS2 的 pty 不是 ConPTY，那边图片是正常的）。

如果某个终端上按键还是不听话，让 mdt 把它收到的东西写下来：

```bat
set MDT_KEY_LOG=C:\Users\me\mdt-keys.log
mdt.exe --gfx=sixel 文档.md
```

这个文件每读一次记一行，十六进制和字符并排，比如上方向键就是 `1b [ A`。一份 bug 报告
需要的正是它：到底什么都没收到，还是收到的是碎片。`tools/keylog_test.py` 在两个平台上
都自动断言同一件事。

### 验证 Windows 构建

Windows 上：`mdt.exe --list-caps`、`mdt.exe demo\demo.md`。

Linux 上配合 Wine 与伪终端驱动：

```sh
export WINEPREFIX=~/.wineprefix        # 首次运行会创建
wine64 build/win/mdt.exe --version
sh tests/windows.sh          # 文本输出、公式、PNG 导出、主题、控制台模式
```

`tests/windows.sh` 会把 `--dump` 的结果与本机构建逐字节对比（Wine 的控制台会插入 CR，
测试会去掉），用内嵌的 QuickJS+MathJax 渲染公式，检查四套主题，并在伪终端里驱动控制台
（`tools/wine_pty_test.py`）：备用屏、鼠标上报、能力探测、状态栏。它同时跑
`tools/keylog_test.py`：在真实控制台里按方向键、滚轮、回车，要求解析器收到的是完整序列。

## 用法

```sh
mdt README.md                   # 读一个文件
mdt --toc notes.md              # 打开时展开大纲面板
mdt --gfx=kitty --theme=light big.md
mdt --dump --width=100 x.md     # 输出纯文本渲染（不用图形）
mdt --screenshot=shot.png x.md  # 把整屏渲染成 PNG
cat notes.md | mdt --dump -     # 从标准输入读取
```

常用参数：

| 参数 | 含义 |
|:--|:--|
| `--encoding=auto\|utf-8\|gbk\|big5\|cp1252\|off` | 非 UTF-8（ANSI）文件的读法（默认 `auto`，按 GBK/GB18030 处理） |
| `--gfx=auto\|kitty\|iterm2\|sixel\|none` | 图形协议（默认 `auto`） |
| `--math=katex\|unicode\|off` | 数学引擎（默认内置 `katex`） |
| `--theme=dark\|light\|nord\|monochrome` | 配色主题 |
| `--toc` | 启动时打开大纲面板 |
| `--line-numbers` | 代码块显示行号 |
| `--no-syntax` | 关闭代码高亮 |
| `--font-px=N` | 覆盖推导出的公式字号 |
| `--dump[=文件]`、`--screenshot=out.png`、`--width`、`--height` | 非交互输出 |
| `--list-caps` | 打印探测到的终端能力 |

完整参数见 `mdt --help`（还有 `--panels`、`--terminal-bg`、`--no-scrollbar`、
`--code-fit`、`--entities`、`--escapes`、`--loose`、`--compat`、`--diag` 等）。

## 快捷键

| 按键 | 作用 |
|:--|:--|
| `j` `k` `↓` `↑` | 上下滚动一行 |
| `space` `b` / `d` `u` | 翻页 / 翻半页 |
| `g` `G` | 到顶部 / 底部 |
| `J` `K` | 下一个 / 上一个标题 —— 大纲面板打开时则是在列表里走，`Enter` 跳过去 |
| `Tab` | 大纲面板。列表比面板长时会明说（表头右边有 `↓`/`↑` 箭头和 `12/47` 计数），可以用 `J`/`K`、滚轮或点击浏览 |
| `/` `n` `N` | 搜索、下一个 / 上一个匹配 |
| `t` | 切换主题（dark、light、nord、monochrome） |
| `m` | 排版公式 ⇄ 纯 Unicode 公式 |
| `r` | 重新读取文件 |
| `o` | 在浏览器里打开文档里的第一个链接 |
| `e` | 把文档文本导出到剪贴板（OSC 52） |
| `?` | 帮助 · `q` / `Ctrl-C` 退出 |

鼠标：滚轮滚动（SGR 和 X10 两种上报都支持），点击右侧滚动条跳转、拖动滚动条实时跟随；
点击链接会弹出可复制/打开的对话框，点击标题、大纲条目可以跳转，点击提示框标题可以
折叠/展开（搜索、大纲、`J`/`K` 跳到折叠框内部时会自动展开）。

## 数学管线是怎么工作的

```
$…$ / $$…$$ 出现在 markdown 里
        │
        ▼
Mathjax TeX→SVG（JS，内嵌）            ← 兼容 KaTeX 语法，TeX 超集
   由二进制里的 QuickJS 执行
        │  <svg viewBox="0 -1562.5 8064.8 2808.5" width="18.2ex">
        ▼
svg.cpp：解析 XML + path（M/C/S/Q/A/Z、transform、matrix、嵌套 <svg> viewport
   —— MathJax 把可伸缩括号画成三段堆叠的内层 <svg>）
   → 展平成多边形 → 非零环绕扫描线光栅化 + 超采样抗锯齿 → 合成 RGBA
        ▼
render.cpp：按终端单元格网格定尺寸，摆在文字基线上
   （em = 0.85 × 单元格高，基线 = 0.76 × 单元格高）
        ▼
term.cpp：kitty/iTerm2 用 PNG，sixel 终端用 sixel，
          按内容哈希缓存，每张图只传输一次
```

尺寸信息取自 SVG 的 `viewBox` 和 `vertical-align:-2.819ex`，和浏览器用的完全一致，
所以行内公式能和周围文字对齐，而不是浮在空中。

## 代码结构

```
src/util.*        UTF-8 + wcwidth 表、字符串、base64、颜色
src/platform.*    唯一面向系统的代码：POSIX 的 tty/窗口/IO 与 Win32 控制台
src/term.*        终端能力探测、带差分刷新的单元格帧缓冲、kitty/iTerm2/sixel
src/md.*          CommonMark 子集 + GFM 表格/删除线/任务列表 + $公式$
src/svg.*         SVG 子集解析 + 软件光栅化
src/math.*        QuickJS 宿主 + 公式度量/位图缓存 + Unicode 降级
src/image.*       stb_image 解码、缩放、PNG 编码、http(s) 下载
src/render*.cpp   文档排版与绘制（文字网格 + 图形叠加层）
src/font.*        --screenshot 用的 TrueType 光栅化
src/app.cpp       事件循环、大纲、搜索、状态栏
src/generated/    内嵌的 JS 包（由 tools/build_assets.py 生成）
vendor/           stb 单文件库、QuickJS-NG
```

`tools/pty_test.py` 在伪终端里驱动程序，逐个检查每种图形协议真的发出了对应的转义序列。
`sh tests/smoke.sh` 不编译任何东西，直接对 `./build/mdt` 跑完整套件（主题、公式模式、
stdin、文本导出、三种协议）。Windows 构建交给 `sh tests/windows.sh` 通过 Wine 跑同一套
（含 `tools/wine_pty_test.py` 走控制台路径），并把渲染结果与本机构建逐字节对比。
`tools/keylog_test.py` 是 issue #3 留下的守卫：它借 `MDT_KEY_LOG` 断言 mdt 交给按键解析器
的**永远是整条序列**，绝不会是一个孤零零的 ESC（有一个孤立的 ESC 会被当成「退出」）。
`tools/async_image_test.py` 打开一份图片放在慢速主机上的文档，检查首帧远低于一秒就出现、
下载期间按键照常响应、每张图只取一次、最终确实画出来。`tools/subcell_align_test.py`
守住行内公式与图片的纵向对齐：亚格偏移被烘进位图，所以不能把图片放在格内像素偏移处的
终端（iTerm2、sixel）和能放的终端画在同样的行上 —— 测试用 `MDT_SNAP_SUBCELL=1` 让
`--screenshot` 以「不带亚格偏移」的方式合成，再要求两张图完全一致。`tools/tall_math_test.py`
守另一半：多行公式（`$\begin{aligned}...$`，行内定界符）会占多个终端行，那么承载它的那一行
要预留这些行（`Line::rows`）；测试在截图里确认没有哪一行像素同时混着公式墨迹和正文墨迹，
且公式块之后的正文从它下面开始。`tools/build_windows.sh` 打 Windows 的发布 zip。

## 性能说明

在一份 250 KB、400 个小节、约 420 个公式（含表格、代码、图片）的文档上实测：

| 阶段 | 用时 |
|:--|--:|
| Markdown 解析 | 6 ms |
| 全文档排版（公式延后） | 21 ms |
| 首帧上屏（含 QuickJS 启动约 150 ms） | 0.57 s |
| 同一文档立刻排所有公式（旧行为） | 2.5 s |
| 滚动一屏的重绘 | < 10 ms（公式位图有缓存） |

公式是懒排版的：只有进入视野的才会被度量与光栅化。如果某个被延后的公式实际比排版时
估计的更大或更小，文档会重新排一次（此时度量已有缓存），所以最终几何是精确的。

## 显示选项

| 参数 | 作用 |
|---|---|
| `--panels` | 给代码块、引用、表头加上带底色的面板（默认关：整页只有一种底色） |
| `--terminal-bg` | 页面底色用 SGR 49，让终端自己的配色/透明透出来 |
| `--no-scrollbar` | 不画滚动条 |
| `--encoding=auto\|utf-8\|gbk\|gb18030\|big5\|cp1252\|latin1\|off` | 非 UTF-8 文件怎么读（默认 `auto`：GBK/GB18030，也就是中文 Windows 的「ANSI」代码页） |
| `--entities=off\|force` | 修复在到你手里之前就被 HTML 转义过的源码（默认关） |
| `--escapes=auto\|off\|force` | 控制上面那条反斜杠转义修复 |
| `--loose=auto\|off\|on` | 接受 `#标题` / `-项目` / `1.项目` 这种没有空格的行首标记 |
| `--code-fit` | 代码框贴着代码长度，而不是撑满整页 |
| `--cell=WxH` | `--dump`/`--screenshot` 用的单元格像素尺寸（默认 9x19），让截图能和你的终端严丝合缝 |
| `--compat` | 不用图形、不用 DEC 2026 同步刷新、不推送键盘协议、不做能力探测 —— 给那些支持有问题的终端 |
| `--diag` | 打印 mdt 眼中的文件（编码、转义、块统计、开头字符）、选中的界面语言及其理由、以及它眼中的终端；没有 tty 也能跑 |
| `--lang=auto\|en\|zh` | 界面语言（默认 `auto`）。`--help`、报错和状态栏都会变成中文；`MDT_LANG=zh` 或配置文件里的 `lang = zh` 同样生效。`--lang=fr` 会被拒绝，而不是悄悄退回默认 |

### 界面语言

`--help`、命令行报错、配置文件的抱怨、交互时的状态栏都有中英文两套。语言按这个顺序决定：

```sh
mdt --lang=zh 文档.md          # 1. 命令行
MDT_LANG=zh mdt 文档.md        # 2. mdt 自己的环境变量
lang = zh                     # 3. 配置文件
LANG=zh_CN.UTF-8 mdt 文档.md   # 4. 系统 locale
```

所以在中文系统上 mdt 会自动说中文，而 `--lang=en` 可以随时切回英文。不认识的语言
（`--lang=fr`）是**报错**而不是静默退回默认；`MDT_LANG` 写了不认识的值会警告一句；
`LANG` 里的怪值不算你的错，不吵。`--diag` 会告诉你最终是哪一条生效的，并给出输入走的是哪条路
（`windows console records` / `windows byte stream` / `posix tty (bytes)`）。`zh_TW`/`zh_HK`
拿到的同样是简体（只有一套中文，暂时没有繁体翻译）。

### 配置文件

每次都敲同样的参数很烦，所以 mdt 会读一个默认值文件：

```
~/.config/mdt/config            # Linux/macOS（$XDG_CONFIG_HOME/mdt/config）
%APPDATA%\mdt\config           # Windows
```

```ini
# mdt 设置 —— 命令行参数总是优先于这个文件
theme = nord
panels = 1
scrollbar = off
line_numbers = on
```

支持的键：`theme`、`gfx`、`math`、`panels`、`terminal_bg`、`scrollbar`、
`line_numbers`、`syntax`、`code_fit`（真值可写 `1 on yes true`）。写错的键或值会在
stderr 上报出来而不是被默默忽略，`mdt --diag` 会告诉你读的是哪个文件；
`MDT_CONFIG=/路径` 可以指定另一个文件。

设置 `NO_COLOR=1`（见 [no-color.org](https://no-color.org)）会切到 monochrome 主题 ——
除非你显式指定了主题。

### Markdown 细节

GitHub 的 alert 语法（`> [!NOTE]`、`[!TIP]`、`[!IMPORTANT]`、`[!WARNING]`、`[!CAUTION]`）
渲染成带加粗标签的引用块；`<details>`/`<summary>` 会变成「标签 + 正文」两段，而不是把两者
粘在一起；`# 标题 {#custom-id}` 里的属性会被去掉。文件开头的 YAML front matter 会被跳过；
引用式链接（`[text][ref]`、`[text][]`、`[ref]`）会按 `[ref]: https://…` 的定义解析 ——
未定义的标签保持原样文字，符合 CommonMark 的约定。

脚注（`[^1]` 与 `[^1]: 注释内容`）按「首次被引用的位置」编号，注释统一排在文末的有序列表里；
`术语` 后面跟 `: 定义` 的行会渲染成定义列表，而不是被并成一大段。`mdt --diag 文件.md`
会报告各识别出多少条。


## 大文档

4 MB 的 Markdown 以前要占 500 MB 内存：解析树、整篇文档「每个屏幕行一个 Line」的排版结果，
外加一张和文档一样高的屏幕（`Screen` 内部有前后两帧的单元格缓冲，光这一项就是 190 MB）。
现在 `--dump` 按屏切片排版并边排边输出，这部分开销基本消失。`mdt --diag 文件.md` 会给出数字：

```
  block struct    : 568 bytes (span 120, line 256, run 168, table cell 96)
  layout          : 80110 rows, 65110 lines, 57212 runs, 0 table cells
  memory          : 83.5 MB peak RSS for the whole document
```

`make mem-report && ./build/mem-report 大文件.md` 会按阶段（解析、排版、run 文本）打印同样的
数字，方便继续优化；`tests/smoke.sh` 里有一条断言：3 MB 文档的内存峰值不得超出预算。

## 命令行参数会被检查

不合理的选项会明确报错，而不是被悄悄忽略：值写错了（比如 `--theme=drak`）会以退出码 2
结束并列出可用取值；尺寸必须合理（`--width` 为 0 表示「自动推导」，1..4096 之间的值被接受，
以前 `--width=1000000` 会让程序申请百万列的帧缓冲然后被 OOM 杀掉）；`--cell` 要求真正的
`宽x高` 像素值；把一个目录当文档传进去会说「`X` is a directory, not a file」，
而不是什么都不渲染还返回 0。命令行里给了两个文件会被拒绝（以前是默默丢掉后一个），
`mdt --dump 大文件.md | head` 也会正常退出 0，而不再被 SIGPIPE 杀死。
这些都有测试覆盖：`sh tests/cli.sh`（已并入 `tests/smoke.sh`）。

## 出问题时排查

| 工具 | 回答什么 |
|:--|:--|
| `mdt --diag 文件.md` | mdt 眼里这个文件是什么样：编码（BOM？UTF-8？GBK？）、换行符、应用了哪些修复、块统计、开头字符的码点 —— 以及 mdt 眼里的终端是什么样。不需要 tty，可直接贴进 issue。 |
| `MDT_FRAME_LOG=/tmp/mdt.log mdt 文件.md` | 把每一帧原样写下来（转义序列展开成可读文本），用来区分「mdt 画错了」和「终端弄丢了」。 |
| `MDT_KEY_LOG=/tmp/mdt-keys.log mdt 文件.md` | 把 mdt 每读一次输入都追加进去，十六进制和字符并排（上方向键是 `1b [ A`），用来区分「终端什么都没发」和「发来的不是一个完整的键」。 |
| `mdt --compat 文件.md` | 关掉所有可选特性（图形、DEC 2026 同步输出、kitty 键盘协议、能力探测），每行从第 0 列整体重绘。如果显示正常了，就是终端在这些里有一个处理不当，`--compat` 就是规避手段。 |

### GBK 等非 UTF-8 文件

中文 Windows 编辑器保存的 Markdown 通常是 GBK（系统「ANSI」代码页），以前每个汉字都会显示成
乱码。现在 mdt 会发现这些字节不是合法 UTF-8，并在解析前转码 —— 标题、列表、代码块结构都保持
完整，`--diag` 也会写明它做了什么：

```
  encoding        : utf-8, no BOM, but 23 invalid byte(s) before offset 200000 - probably not UTF-8 (GBK/ANSI?)
                    -> converted from gb18030 to UTF-8 for rendering
```

`--encoding=` 可以覆盖自动判断：`gbk`、`gb18030`、`big5`、`cp1252`、`latin1`，
或 `off`（保持原样）。自动判断默认按 GBK 处理，因为 GB18030 的字节空间是 Big5 的超集 ——
Big5 文件会被「成功」转成错误的汉字，所以 Big5 文档请显式加 `--encoding=big5`。

## 已知限制

* 行间公式居中并以位图绘制；过宽的公式会缩放到内容宽度以内。
* sixel 图片只能对齐单元格（sixel 光标无法按亚单元格移动）；kitty 和 iTerm2 可以按像素放置，
  因此能精确落在文字基线上。
* 引用式链接的定义从 0.3.9 起会解析（`[text][ref]`、`[text][]`、`[ref]`），未定义的标签
  保持原样文字；`_斜体_`/`**加粗**` 在单词内部按 CommonMark 规则处理。
* 脚注用 `[^标签]` 引用、`[^标签]: 内容` 定义；定义列表用 `术语` + `: 定义` 两行写法。
  两者都按 GitHub / Pandoc 的习惯处理，文末的注释列表会自动编号。
* HTML 块会转换成对应的 Markdown，未知标签会丢弃但保留其中的文字。
* Windows 上需要支持 VT 的终端（Windows Terminal、Windows 10 1703+ 的 conhost），
  阅读器会自己打开 VT 处理，所以 `cmd.exe` 也能用。kitty/iTerm2 协议是 POSIX 终端的事 ——
  Windows 上可以用 sixel（Windows Terminal 1.22+、WezTerm、mintty）或 Unicode 公式降级。
* 洛谷的提示框默认折叠，指令上写 `{open}` 则默认展开；Bilibili 视频嵌入不会播放
  （这是个纯文本 + 图片的阅读器）。
* 窗口缩放：POSIX 用 SIGWINCH，Windows 轮询 `GetConsoleScreenBufferInfo`。

## 更多文档

| 文件 | 内容 |
|:--|:--|
| [CHANGELOG.md](CHANGELOG.md) | 每个版本改了什么（0.3.8 是一次安全修复，务必看） |
| [SECURITY.md](SECURITY.md) | 漏洞怎么报，以及 mdt 拿到文档后都做了什么 |
| [CONTRIBUTING.md](CONTRIBUTING.md) | 构建、测试、提交信息的约定 |
| [docs/mdt.1](docs/mdt.1) | man 手册（`make install` 会装到 man1） |

## 许可

MIT 许可，见 [LICENSE](LICENSE)。二进制里还内嵌了别人的工作
（QuickJS-NG — MIT、MathJax — Apache-2.0、KaTeX 字体 — SIL OFL 1.1、stb — MIT/公有领域），
具体清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，完整许可证文本在
[licenses/](licenses/)。
