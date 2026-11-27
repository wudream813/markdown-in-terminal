class Mdt < Formula
  desc "Terminal Markdown reader: typeset maths, images, syntax highlighting"
  homepage "https://github.com/wudream813/markdown-in-terminal"
  version "0.3.27"
  license "MIT"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and fully
  # static builds on Linux for both x86-64 and arm64 (no glibc version to
  # satisfy either way).
  on_macos do
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.27/mdt-macos-arm64"
      sha256 "04fe2f55c69c6a74dee9f1dcc43b27b1340dcb3d7832ace7e56ead8e9b490827"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.27/mdt-linux-amd64"
      sha256 "16d3ef534003efc7ff0014746b8621e9d146eb1ca001fa1de653130d17543702"
    end
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.27/mdt-linux-arm64"
      sha256 "ec5bc6f37092483ee225a56f2d833491ec2a68459082f7499913f6477bedecb0"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.27/docs/mdt.1"
    sha256 "252465a4f466dad5f45cfa79da4039e078c6c712b08b42ee3cbb007e41ec633b"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.27/completions/mdt.bash"
    sha256 "459f234d014740b9175043a080336dcf1a1fc3d6151c5c7b7dceab7e055902c4"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.27/completions/_mdt"
    sha256 "c2ee2264f689290180842b9d67425ccde66c7e684fe32e38fe1f78d662196e46"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.27/completions/mdt.fish"
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
