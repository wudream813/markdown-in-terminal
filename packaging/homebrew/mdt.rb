class Mdt < Formula
  desc "Terminal Markdown reader: typeset maths, images, syntax highlighting"
  homepage "https://github.com/wudream813/markdown-in-terminal"
  version "0.3.23"
  license "MIT"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and fully
  # static builds on Linux for both x86-64 and arm64 (no glibc version to
  # satisfy either way).
  on_macos do
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.23/mdt-macos-arm64"
      sha256 "4fd1924f53a70e8fe2fc5f9832a2f4d2df84f9290c1776c14b58e3cdb8dcafb6"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.23/mdt-linux-amd64"
      sha256 "01c4b3b3c9e3278639b1207b4b2142a290a4016e528adb5de33eac0ba30a56ee"
    end
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.23/mdt-linux-arm64"
      sha256 "c90a9b4417a2b227029baf51db432c39e96b6432c244a758ac75eb437ec67ad8"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.23/docs/mdt.1"
    sha256 "252465a4f466dad5f45cfa79da4039e078c6c712b08b42ee3cbb007e41ec633b"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.23/completions/mdt.bash"
    sha256 "459f234d014740b9175043a080336dcf1a1fc3d6151c5c7b7dceab7e055902c4"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.23/completions/_mdt"
    sha256 "c2ee2264f689290180842b9d67425ccde66c7e684fe32e38fe1f78d662196e46"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.23/completions/mdt.fish"
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
