class Mdt < Formula
  desc "Terminal Markdown reader: typeset maths, images, syntax highlighting"
  homepage "https://github.com/wudream813/markdown-in-terminal"
  version "0.3.22"
  license "MIT"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and fully
  # static builds on Linux for both x86-64 and arm64 (no glibc version to
  # satisfy either way).
  on_macos do
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.22/mdt-macos-arm64"
      sha256 "1e9d2abfbc01b23041a431ff35247bde9c0a17c89f24fca46fb31d3fd47dd8e6"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.22/mdt-linux-amd64"
      sha256 "b51ad91a5e41af0280a5931a8b8cc938f4baed041d41e35b1cd9a442c7705a2d"
    end
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.22/mdt-linux-arm64"
      sha256 "41b8af57af6353d85d6f446540ba58faac85f1bc9d3e647e1520d96814932e87"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.22/docs/mdt.1"
    sha256 "252465a4f466dad5f45cfa79da4039e078c6c712b08b42ee3cbb007e41ec633b"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.22/completions/mdt.bash"
    sha256 "459f234d014740b9175043a080336dcf1a1fc3d6151c5c7b7dceab7e055902c4"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.22/completions/_mdt"
    sha256 "c2ee2264f689290180842b9d67425ccde66c7e684fe32e38fe1f78d662196e46"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.22/completions/mdt.fish"
    sha256 "86d01a45e83e42a9539b0a16b82da7253c9080f9805e5c355c435ed9e0175e64"
  end

  def install
    binary = if OS.mac?
               "mdt-macos-arm64"
             elsif Hardware::CPU.arm?
               "mdt-linux-arm64"
             else
               "mdt-linux-amd64"
             end
    bin.install binary => "mdt"

    man1.install resource("man")
    bash_completion.install resource("completion-bash") => "mdt"
    zsh_completion.install resource("completion-zsh") => "_mdt"
    fish_completion.install resource("completion-fish") => "mdt.fish"
  end

  def caveats
    <<~EOS
      mdt is not notarised by Apple, so a copy that has been quarantined (that
      is, downloaded with a browser) will be refused by Gatekeeper.  Homebrew's
      own download is not quarantined; if you moved a copy in yourself, clear
      it with:

        xattr -d com.apple.quarantine #{bin}/mdt

      Remote images are fetched with the curl(1) command, and links are opened
      with xdg-open(1) (Linux), so having curl and xdg-utils installed helps.
    EOS
  end

  test do
    assert_match version.to_s, shell_output("#{bin}/mdt --version")
    (testpath/"t.md").write "# heading\n\nsome *text*\n"
    assert_match "heading", shell_output("#{bin}/mdt --dump #{testpath}/t.md")
  end
end
