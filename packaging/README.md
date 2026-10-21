# 打包与分发

`packaging/` 里是**真实会被发布出去的清单文件**，不是模板 —— Scoop、Homebrew、winget、AUR
各自要的东西都在这里，版本号和 sha256 由脚本从已发布的 release 上取。

| 渠道 | 用户怎么装 | 状态 |
|---|---|---|
| **Scoop**（Windows） | `scoop bucket add wudream813 https://github.com/wudream813/scoop-bucket`<br>`scoop install wudream813/mdt` | 仓库已建，清单待推（见下） |
| **Homebrew**（macOS / Linux） | `brew tap wudream813/mdt`<br>`brew install mdt` | 仓库已建，清单待推（见下） |
| **winget**（Windows） | `winget install wudream813.MarkdownInTerminal` | 清单已就绪，需向 `microsoft/winget-pkgs` 提 PR |
| **AUR**（Arch） | `yay -S mdt-bin` | 清单已就绪，需 AUR 账号 + SSH key 推送 |
| **GitHub Release** | `curl -fsSL …/main/install.sh \| sh` | **已上线**（0.3.9 起，另附 `SHA256SUMS`） |

## 目录

```
gen.py                  从一个 tag 重新生成全部清单（下载产物 → 算 sha256 → 重写文件）
pkgs.py                 从发布产物打 .deb 与 .rpm（dpkg-deb + rpmbuild，四种包一次出）
verify.py               把清单里每个 url/sha256 真的下载一遍、比一遍（发布前跑它）
scoop/mdt.json          Scoop manifest（装 release zip，把 mdt.exe 放进 PATH）
homebrew/mdt.rb         Homebrew formula（macOS arm64 + Linux x86-64/arm64，含 man page 与补全）
winget/*.yaml           winget 的三件套（version / installer / defaultLocale）
aur/{PKGBUILD,.SRCINFO} AUR 的 mdt-bin（x86_64 与 aarch64 静态二进制，无依赖，带 optdepends）
```

`gen.py` 和 `verify.py` 只依赖 Python 3 标准库，跟仓库其它工具一样；
`pkgs.py` 另需 `dpkg-deb`（dpkg 自带）与 `rpmbuild`（`rpm` 包）。

## .deb 与 .rpm

```sh
python3 packaging/pkgs.py v0.3.20                     # 下载发布产物再打包
python3 packaging/pkgs.py v0.3.20 --from dist --out dist   # 用已有文件（CI 就是这么调的）
```

一次出四个：`mdt_<版本>-1_amd64.deb`、`mdt_<版本>-1_arm64.deb`、
`mdt-<版本>-1.x86_64.rpm`、`mdt-<版本>-1.aarch64.rpm`。包里是二进制 + man page +
三个补全；因为是静态二进制，**依赖是空的**，`curl` 与 `xdg-utils` 只写成
Recommends。aarch64 的 rpm 由 `rpmbuild --target aarch64` 在 x86-64 上直接打出来
（这里不编译任何东西，payload 是现成的），所以 CI 不需要 ARM runner。

## 发布一个新版本时

```sh
# 1. tag 已推、release workflow 跑绿之后：
python3 packaging/gen.py v0.3.10          # 重新下载产物、重算哈希、重写全部清单
python3 packaging/verify.py               # 每个 url 都能下、每个 sha256 都对得上
git add packaging && git commit -m "packaging: 0.3.10"

# 2. 推两个独立仓库（Scoop bucket 与 Homebrew tap）
GITHUB_TOKEN=… python3 packaging/gen.py v0.3.10 --publish

# 3. winget 与 AUR 手工提（见下，AUR/winget 无法从脚本推）
```

`gen.py --check` 只报告差异不写文件，`--from DIR` 用本地已下载的文件省一次流量。
`verify.py` 是唯一会去碰网络的检查，也是唯一能发现「清单写对了但 tag 上没这个文件」的检查。

## Scoop

清单指向 `mdt-<version>-win64.zip`，`extract_dir` 是解压后的目录名，`bin` 是里面的 `mdt.exe`；
`checkver: github` + `autoupdate` 让 `scoop update mdt` 能自动跟上新版本，所以 bucket 仓库只有
这一个文件加一个 README。

```powershell
scoop bucket add wudream813 https://github.com/wudream813/scoop-bucket
scoop install wudream813/mdt
mdt --list-caps
```

## Homebrew

formula 装的是发布产物：macOS 走 `mdt-macos-arm64`，Linux 按架构走静态的 `mdt-linux-amd64`
或 `mdt-linux-arm64`，另外用 `resource` 从同一个 tag 取 `docs/mdt.1` 和三个补全脚本，一起装到标准位置。

```sh
brew tap wudream813/mdt
brew install mdt
```

两点需要注意：

* **macOS 只有 Apple 芯片**（Intel Mac 没有产物），其它架构 formula 会直接报错说没有产物（比装上一个
  跑不起来的二进制好）。Linux 现在 x86-64 与 arm64 都有静态产物。
* 二进制**没有做 Apple 签名/公证**。Homebrew 自己下载的文件不带 quarantine 属性，能直接跑；但如果
  你从浏览器下载过一份再挪进来，Gatekeeper 会拦，`xattr -d com.apple.quarantine "$(which mdt)"` 可解。
  formula 的 `caveats` 里写了这句。

## winget

`packaging/winget/` 的三个文件就是 `microsoft/winget-pkgs` 里需要提交的内容，路径是：

```
manifests/w/wudream813/MarkdownInTerminal/0.3.9/wudream813.MarkdownInTerminal.yaml
manifests/w/wudream813/MarkdownInTerminal/0.3.9/wudream813.MarkdownInTerminal.installer.yaml
manifests/w/wudream813/MarkdownInTerminal/0.3.9/wudream813.MarkdownInTerminal.locale.en-US.yaml
```

提交方式二选一：

```powershell
# A. wingetcreate（推荐，会自己 fork、校验、开 PR）
wingetcreate update wudream813.MarkdownInTerminal --version 0.3.9 --urls https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.9/mdt-0.3.9-win64.zip

# B. 手工：fork microsoft/winget-pkgs → 拷上面三个文件到对应目录 → 开 PR
```

首次提交需要**签 Microsoft CLA**（PR 里机器人会给链接），这一步只能账号本人做，脚本代替不了。
清单用的是 zip + `NestedInstallerType: portable`，装完 `mdt` 可直接敲。

## AUR

`mdt-bin`：`depends=()`（静态链接，运行时不依赖任何东西），`optdepends` 里是 `curl`（抓文档里的
远程图片）和 `xdg-utils`（打开链接）；`options=('!strip')` 因为产物本来就是 strip 过的静态二进制。

```sh
# 需要 AUR 账号 + 已注册到账号的 SSH key
git clone ssh://aur@aur.archlinux.org/mdt-bin.git
cp packaging/aur/PKGBUILD packaging/aur/.SRCINFO mdt-bin/
cd mdt-bin && makepkg --printsrcinfo > .SRCINFO && git add -A && git commit -m "0.3.9" && git push
```

`.SRCINFO` 必须和 `PKGBUILD` 一致（AUR 只看它），这里手写了一份，`verify.py` 会检查两者不打架；
真正推送前用 `makepkg --printsrcinfo` 重新生成一次最稳。

## 还缺什么

* ~~Linux 只有 amd64、macOS 只有 arm64~~ —— **Linux arm64 已在 0.3.17 补上**（交叉编译 + qemu
  冒烟测试，发布文件 `mdt-linux-arm64`，Scoop 之外的三个渠道都覆盖到了）。macOS 仍只有 arm64
  （Intel Mac 需要 x86-64 交叉编译，暂无需求）。
* ~~Debian 包~~ —— **已在 0.3.18 补上**（`packaging/pkgs.py`：.deb 与 .rpm，各两种架构）。
  它们不进任何包仓库：装法是直接下 release 里的文件（README 里有一行命令）。
* macOS 未签名公证：要 Apple Developer ID 才能根治，不是改代码能解决的。
* 其余候选：把 `.deb`/`.rpm` 推进某个仓库（Debian 要 mentor + NEW 队列、Fedora 要 COPR），
  以及 `nixpkgs`、Alpine。
