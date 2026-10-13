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
| Linux x86-64 | `mdt-linux-amd64` | 需要 glibc ≥ 2.38（Ubuntu 24.04+、Debian 13+、Fedora 39+）；更老的发行版请从源码构建 |
| macOS Apple 芯片 | `mdt-macos-arm64` | 未签名未公证，下载后执行一次 `xattr -d com.apple.quarantine mdt` |
| Windows x64 | `mdt-win64.exe` | 或者 `mdt-0.3.8-win64.zip`（里面还带 demo） |

```sh
# Linux
curl -fL -o mdt https://github.com/wudream813/markdown-in-terminal/releases/latest/download/mdt-linux-amd64
chmod +x mdt && sudo install -m 0755 mdt /usr/local/bin/mdt
mdt --list-caps          # 看看探测到的单元格尺寸、图形协议、字体
mdt 文档.md

# macOS（Apple 芯片）
curl -fL -o mdt https://github.com/wudream813/markdown-in-terminal/releases/latest/download/mdt-macos-arm64
chmod +x mdt && xattr -d com.apple.quarantine mdt 2>/dev/null
sudo install -m 0755 mdt /usr/local/bin/mdt
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

`.exe` 用 `-static -static-libgcc -static-libstdc++` 链接，只依赖 `KERNEL32.dll` 和
`msvcrt.dll`，没有需要一起分发的 MinGW 运行库。Windows 上远程图片走系统自带的
**WinINet**（不需要 curl.exe，也没有 shell）。字体从 `%WINDIR%\Fonts` 里找
（正文用 Consolas/Cascadia Mono，宽字符用 MS YaHei / SimSun / Noto CJK）。

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
| `J` `K` | 下一个 / 上一个标题 |
| `Tab` | 大纲面板（`Enter` 或点击跳转到标题） |
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

## 出问题时排查

| 工具 | 回答什么 |
|:--|:--|
| `mdt --diag 文件.md` | mdt 眼里这个文件是什么样：编码（BOM？UTF-8？GBK？）、换行符、应用了哪些修复、块统计、开头字符的码点 —— 以及 mdt 眼里的终端是什么样。不需要 tty，可直接贴进 issue。 |
| `MDT_FRAME_LOG=/tmp/mdt.log mdt 文件.md` | 把每一帧原样写下来（转义序列展开成可读文本），用来区分「mdt 画错了」和「终端弄丢了」。 |
| `mdt --compat 文件.md` | 关掉所有可选特性（图形、DEC 2026 同步输出、kitty 键盘协议、能力探测），每行从第 0 列整体重绘。如果显示正常了，就是终端在这些里有一个处理不当，`--compat` 就是规避手段。 |

## 已知限制

* 行间公式居中并以位图绘制；过宽的公式会缩放到内容宽度以内。
* sixel 图片只能对齐单元格（sixel 光标无法按亚单元格移动）；kitty 和 iTerm2 可以按像素放置，
  因此能精确落在文字基线上。
* 引用式链接 `[text][ref]` 的**定义目前还没有解析**（正文会显示，但点开用的是
  `[ref]` 里的文字），`_斜体_`/`**加粗**` 在单词内部按 CommonMark 规则处理。
* HTML 块会转换成对应的 Markdown，未知标签会丢弃但保留其中的文字。
* Windows 上需要支持 VT 的终端（Windows Terminal、Windows 10 1703+ 的 conhost），
  阅读器会自己打开 VT 处理，所以 `cmd.exe` 也能用。kitty/iTerm2 协议是 POSIX 终端的事 ——
  Windows 上可以用 sixel（Windows Terminal 1.22+、WezTerm、mintty）或 Unicode 公式降级。
* 洛谷的提示框默认折叠，指令上写 `{open}` 则默认展开；Bilibili 视频嵌入不会播放
  （这是个纯文本 + 图片的阅读器）。
* 窗口缩放：POSIX 用 SIGWINCH，Windows 轮询 `GetConsoleScreenBufferInfo`。

## 许可

MIT 许可，见 [LICENSE](LICENSE)。二进制里还内嵌了别人的工作
（QuickJS-NG — MIT、MathJax — Apache-2.0、KaTeX 字体 — SIL OFL 1.1、stb — MIT/公有领域），
具体清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，完整许可证文本在
[licenses/](licenses/)。
