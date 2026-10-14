class Mdt < Formula
  desc "Terminal Markdown reader: typeset maths, images, syntax highlighting"
  homepage "https://github.com/wudream813/markdown-in-terminal"
  version "0.3.9"
  license "MIT"

  # Prebuilt, self-contained binaries: Apple silicon on macOS, and a fully
  # static x86-64 build on Linux (no glibc version to satisfy).
  on_macos do
    on_arm do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.9/mdt-macos-arm64"
      sha256 "6a5e21fab03bbda44a0c97a6719fd84e1f84b5ac1d6d15b256b3bf143b7ffe71"
    end
  end

  on_linux do
    on_intel do
      url "https://github.com/wudream813/markdown-in-terminal/releases/download/v0.3.9/mdt-linux-amd64"
      sha256 "376a4f0f87d9a976cd9b75128c90db4c15935b0396f06fff2103a2a5fc226806"
    end
  end

  resource "man" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.9/docs/mdt.1"
    sha256 "73582ea60fc89aebf79f9eb7850f3b38e0d7da83015522ae3512e0dd69754446"
  end

  resource "completion-bash" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.9/completions/mdt.bash"
    sha256 "8e957355edf33c95a3b669d7918205b69413048b320f0ded178b8f27f3380fe6"
  end

  resource "completion-zsh" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.9/completions/_mdt"
    sha256 "07639f05a5fc36072bcad6684fdade2877886050d36fcd0bff3e4e6a358cbaf4"
  end

  resource "completion-fish" do
    url "https://raw.githubusercontent.com/wudream813/markdown-in-terminal/v0.3.9/completions/mdt.fish"
    sha256 "a8501db39feff0a9deb755d6ca0098c435348a3574d98fbf6487c1360ccd630f"
  end

  def install
    binary = OS.mac? ? "mdt-macos-arm64" : "mdt-linux-amd64"
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
